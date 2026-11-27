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
| Joraga Warcaller | 3 | vanilla_creature; `multikicker_cost {1}{G}`, `multikick_enter_counters`, lord `power/tough_bonus 1` + `lord_per_own_plus_counter`, `lord_excludes_self` | the DB synthesizes "<name> (Kicked xK)" faces K = 1..20 (printed + K x {1}{G}, `kick_times`); `Action::kick_count` / `CastRiders::kick_count` swap the face in both cast sites (`KickedFaceFor`); K counters in the as-enters window of BOTH enter sites; `ComputeLordBonus` multiplies the lord bonus by the counters on the Warcaller, read live | A: K = main_phase plan variants + GUI K cycling |
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
* Multikick / squad faces stop at 20 kicks (Warcaller x20 = 41 mana; USER 2026-11-27: "I think Kick counts should be
  capped at lethal size of 20"; was 16). A model bound, flat in both formats (2HG's 30 life is not separately bounded).
* Visionary beyond the horizon (USER 2026-11-27: "Leaf crowned-visionary should probably not decline the draw
  automatically even beyond the search horizon", "If you don't have anything else to use the mana for it should
  draw."): a greedy plan (horizon playout / d0 runner) carries `MihailPayChoices::pay_spare`, and an unpinned trigger
  pays when the {G} is spare -- the rest of the line's casts (`g_line_unpaid_cost`), the plan's activations, and before
  combat the combat pump count (Ezuri) all stay payable (`PayDrawPlayoutSpare`). Searched turns still branch both ways
  (unpinned = decline there). Was: never pay beyond the horizon.
  Measured 2026-11-27 (pooled A/B, d5 b20, std 1500 + 2HG 750 paired games, seed 9800001+gi): std delta +0.0000
  (0 slower / 0 faster; play moved in 5 of 30 chunks), 2HG -0.0013 (0 slower / 1 faster; 3 of 15 chunks); units
  +0.65% std / +0.26% 2HG. Smoke: only `elves_smoke_d0_s1001` moved (4.8910 -> 4.8950, 18 slower / 14 faster), all of
  it this policy (selector off reproduced GT). Mechanism of the d0 slower games (gi130/185/226): a PRE-COMBAT spare {G}
  was paid by tapping attack-ready mana creatures (Llanowar Elves, Elvish Mystic, Priest of Titania), costing their
  combat damage (8->2, 13->7, 4->0). AMENDED (the USER's rule applied: an attack-ready mana creature has another use
  before combat): before combat the {G} must be payable without tapping an untapped, attack-ready creature with
  power > 0, and the real payment holds the same set; when the plain payment taps none, nothing changes. Measured
  (paired, seed 9900001+gi, amendment on vs the plain spare test): d0 -0.0085 (6 slower / 23 faster of 2000), std d5
  b20 +0.0000 (0/0 of 1500, units -0.53%), 2HG d5 b20 +0.0000 (0/0 of 750, units -0.41%).
* Magistrate's AUTONOMOUS target set is the board the plan starts from: a creature cast earlier in the same
  plan cannot be the target in that main (the human picks on the live board, so the viewer is not affected).
  The same holds for Kessig Wolf Run and the Merfolk tap-target pumps. A completeness gap to raise with the
  USER: it is a missing line, not a pruned one.
* Visionary + Mihail in one list: the Visionary fan is skipped (one shared ordinal stream). No list runs both.
* World-rule simultaneous-entry tie not modelled (inert: Genesis Wave's put set is a choice).
* Search-cost watch for Stage 5: the multikick fan (up to 20 variants per Warcaller / Brigade in hand), the
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
* **Hold-back rule (AMENDED 2026-11-26 on wip/elves-scal, pending the USER; `logs/elves/scaling-amend.md`):** the
  attackers a lower count could leave untapped are EVERY untapped mana creature of ours that can attack this turn
  (`CanAttackFull`: not summoning-sick, or hasted by Concordant Crossroads): Llanowar / Mystic at 1, and the
  scaled dorks at their live tap (Priest / Archdruid at the Elf count, Marwyn at its power, each grown by the
  plan's other Elf casts). H = the sum of those yields; h = ceil(H / mana per count step) (Wave 1, a kick or squad
  copy 2, Ezuri's pump 5). The band runs from the max down to the count that leaves all of them untapped. **Pruned
  when attacking cannot matter:** post-combat, and with no attack-capable mana creature. *First cut (superseded):*
  only 1-mana dorks, capped at 2 -- it lost 9 of 3,000 held-out games, each a line holding back a scaled dork or
  three or more attackers, none findable at d8 b0; a cap of two attackers of any kind still lost std 1957 of the
  fresh 3,000 (four held attackers) and 2HG 689 of the first sample (three).
* **Lift switches:** `MTG_ELVES_SCALING_PRUNE=0` (heurarm `ELVES_SCALING_PRUNE`, per job; default ON) -> every
  count and split. `MTG_UNPRUNED`, and `MTG_UNPRUNE=replicate` (the gate the multikick fan already shares -- the
  64-gate mask is full) or `=xspell` (Wave's X), lift it. Human play never sees it (the host skips it under
  `HumanPlayActive()`), and the engine still offers the human every K and X.
* **Lines it can miss:** two sinks scaled together (Warcaller x2 + Brigade x1, Wave + a kicked Warcaller); a
  count below the band that frees mana for something that is not a plan action -- Visionary's {G} draws (paid at
  trigger time), Ezuri's combat pump from leftover mana (only the no-sink / K = 0 plan keeps "Ezuri fully
  scaled"), a post-combat activation, Wirewood Lodge's untap; holding back a creature cast THIS turn with haste
  (not on the battlefield when the band is sized); a smaller Wave chosen to keep the other Waves in the library. The hold-back variants only help
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
* **Signature key:** the autonomous plan-dedup signature did not key Genesis Wave's X, so the dedup kept ONE X per cast set (the first enumerated). ElvesProvider first opted in to a `#WX` key; 2026-11-26 (`wip/wavex-sig`) the opt-in was DELETED and EVERY {X} cast now keys its X (`#XV`, `CastKeysX` in TurnSolver.cpp, both signature builders), so the collapse is gone for every deck. Elves' partition is unchanged (the `#WX` key it replaces split the same classes). Measured on the base binary with `MTG_SIG_X_AUDIT=1`: the live collapse was Selesnya Lifegain only (std + 2HG: the un-grown and grown Wave max differ when its Elves grow the scaled dorks); Landfall never collided (no scaled dork, no X reducer, so the two maxima are equal). Under `MTG_UNPRUNED=xspell` the same collapse also hid every Crackle with Power / Unexpectedly Absent X but one. Write-up: `logs/wavex/report.md`.
* The definitive with/without A/B (Stage 5e/5f) is the coordinator's.

## Claude-play sweep

commit: `948933d6c` (benchmark d5 b200). seeds: 9500001..9500018 (disjoint from every suite tier; seed only --
--game-index does not vary a --claude-play game). games: 18. Driver: 6 Opus player agents x 3 games (Claude session).
Result: Claude faster 1 (s9500006, 4 vs 5), equal 17, slower 0. Rules / card-data violations: 0.
Full per-seed tally, notes and repro CSVs: `logs/elves/sweep-tally.md`.

Flags (each verified against cards.json + the rules skill; each became a fix branch, all now merged on `wip/elves-int`):
1. fd-diverge s7036 (breakpoint resume reset the Visionary cast-trigger ordinal): FIXED 430997dae (lockstep #20).
2. Continuation casts' Visionary pay-{G} choice not pinned (ContPins): FIXED 5dd44269d, a74aa0754, d57236f5f
   (lockstep #21). A/B 2000 g net -2, 0 slower; smoke identical.
3. Genesis Wave puts open no re-solve, s9500006 misses T4 at every budget incl. d3 b0: FIXED 43e680701, 50bb7a692,
   bc0a580b1 (hatch MTG_BP_PUT_BATTLEFIELD deleted; declined-search mark folded). s9500006 now T4 at d5 b200/b0, d3 b0.
   Held-out A/B (seed 9600001, d5): creature_giving -0.020, cg2hg -0.015, melira -0.010, elves -0.010, landfall /
   selesnyav2 0, elementals +0.0025, pd +0.0025, pd2hg +0.005 (net >0 = 1-2 games each); ALL 19 slower games (10 A/B
   + 9 smoke) recover at d8 b0 (budget, not a hole). Completeness fix => adopted as a play change. Cost: units/game
   1.455x creature_giving, 1.415x cg2hg, 1.33x melira, 1.29x landfall, 1.19x hydras (ramp spells now open site 10).
4. Immaculate Magistrate targets only the pre-cast battlefield (fixture mag4.json): FIXED 022db4183; smoke identical;
   A/B Elves/Maze/Merfolk 0 change.
5. Mana cluster (hasted scaled-dork credit; grown-scaler prepay; Lodge float feed; Cavern `wild` as {C} on non-creature
   payments; failed held re-solve restores `produced`): FIXED 7ce20cec3, 974493650 (lockstep #23 multikick/squad count
   in the bp record), cbe68653f, 6cc44b7e0, 1fe903e91, 9045dcb4e (four levers deleted), lockstep #22 doc. A/B 41,200 g:
   every deck net <= 0 (Elves d5 -16/1000, d0 -75/4000; Stompy d0 -4; Selesnya d0 -2, d3 -1, d6 -2); 0 slower at any
   searched depth; 5 Elves d0 slower all from #22a and all recover at d5 b20 and d8 b0 (d0 policy: summed-value ranking
   does not see the scaling-sink rule; lethal projection under-reads a lord stack -> follow-up `wip/elves-lethal`).
   Deletion identity: 3900/3900 digests equal the lever-ON arm.
6. Cosmetic labels + validate_line `act=`: FIXED in the contpin picks.
Payer over-taps in human play (s9500002, s9500007, s9500014) are the standing mana-payment exemption, not flags.

flags: 0 unresolved

## Stage 5 verification verdicts

All on the integrated tree `wip/elves-int` (origin 9e566000b + every Elves fix), 2026-11-26.

* **5a mismatch harnesses:** Elves fd sweep, 200 games (seeds 7001+off, d5 b20, `MTG_FD_ORACLE=1 MTG_FLAG_NONCONV=1`):
  0 `[fd-diverge]`, 0 `[nonconv]`. `verify_deck.py` mismatch: clean across seeds [7001, 8001] x 60, both arms.
* **5b multi-depth sanity** (accepted GT): d0 4.885-4.905 (2,000-game rows), d3 4.33-4.40, d5 4.32-4.45; 2HG d5
  4.75-4.83. Monotone in depth. Distribution at d5: mostly T4 (~65%) / T5 (~30%), a few T3. Outliers read: the d5
  non-wins (4 of ~1,260 overnight+regression games) are land screws -- e.g. regression d5 s3003 gi22 kept Wirewood
  Lodge alone (colourless) and cast nothing until T7; a mulligan-profile question (no keep table yet), not play.
* **5c budget:** every slower game in the smoke/regression audits of this landing recovers at d8 b0 (19 Genesis
  Wave games, 5 Elves d0 mana games at d5 b20 / d8 b0, 9 smoke + 10 regression games on the integrated binary).
* **5c2 horizon-honest tie-break: KEEP THE DEFAULT (neutral).** The first two runs were killed by memory (a
  single-step plan-list blow-up in rare Elves positions: 241K Brigade squad-count plans x the Visionary pay fan,
  ~9 GB, filtered only afterwards). Fixed on `wip/elves-mem` ddc45ce91 (the Elves scaling filter also runs
  BEFORE the subdecision-axis fan via `DecisionProvider::PlanListFilterBeforeAxes`; play byte-identical, smoke
  0 changed); the full `leaf_tiebreak_check` batch then ran to completion (24 x 1000 games, RSS 2.1-6.8 GB).
  Result: all 12 paired seed blocks have IDENTICAL avg win turns leaf on vs off (12,000 paired games); digests
  differ in 8 of 12 blocks, so the tie-break fires but never moves a win turn here. Not "never fired"; neutral.
  `GradesNoWinLeaf()` stays default ON. (Measured on the pre-lethal integration binary + the memory fix.)
* **5d claude-play sweep:** 18 games, Claude faster 1 / equal 17 / slower 0; 5 flags, all fixed (above).
* **5e/5f scaling-sink prune (USER rule) held-out A/B** (seeds 9700001+gi, d5 b20 with profile): std 2000 g prune
  4.3415 vs full 4.3385 (net +0.0030, 0 faster / 6 slower), 2HG 1000 g 4.6540 vs 4.6510 (+0.0030, 0 / 3); units
  x0.92. Kept ON (USER's rule); the 9 losses are being root-caused with an amended rule (`wip/elves-scal`).
* **5h viewer surface:** `verify_deck.py` viewer self-guard + surface sweep clean; 4 types wired (dig, dragon,
  firebreathe, target); play_invariants 8 games / 148 decisions hold.
* **5i discard buckets:** draft `MTG_ELVES_BUCKET_DISCARD` (default OFF) moved 0 of 800 paired games; even the
  shed-worst bound moved 0 (95% ceiling 0.375% of games); real cleanup discards ~1 per 400 games. Recommendation:
  do not adopt; Elves keeps the inherited SelesnyaLifegain buckets. USER decision.
* **5j suite membership:** all three tiers with GT (a7516ef44); cost 1499.1 ms/game vs hinata 3616.2 in the same
  run = 0.41x (limit 3x).
* **`verify_deck.py --no-network`: GATE PASS.**

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

## Coordinator checkpoint 5 (2026-11-26 ~13:30 UTC, pre-compaction) -- RESUME HERE

INTEGRATION BRANCH IS NOW `wip/elves-int` (worktree `.claude/worktrees/elves-int`): wip/elves-analysis REBASED onto
origin/phase-1-2-deck-analyzer 9e566000b (392 commits newer). Conflicts resolved: SpellEffects.h (kept origin's
PrintedSpellManaValue + our squad/KickedFaceFor; tutor match = origin's TutorPutTargetMatches, which already has
wish_requires_name), Permanent.h enum (TapFight then TeamBasePT), TurnSolver.cpp (activation table both; PlanDumpModeName
fixed to "SelfPing","TapPing","TapFight","TeamBasePT" -- origin lacked TapPing), HeuristicArm.h both, DecisionProvider.h
both, DecisionProviders.cpp (origin side + our ElvesProvider block appended at EOF). Then cherry-picked: fd (430997dae,
9b0fe940b lockstep #20), contpin (5dd44269d lockstep #21, a74aa0754, d57236f5f), tapt 022db4183. UNCOMMITTED there:
test/regression_cases.sh (Elves rows) + test/suite_cost.json (kept ORIGIN's hinata 18918.94; re-run
`suite_gate.py --cost decks/Elves` on the integrated tree). Build of elves-int queued on heavy.lock
(build/scratch/elves-int/build.N.log). NOT YET BUILT/VALIDATED.

Agent results: contpin DONE (A/B 2000g net -2, 0 slower, smoke 243/243 identical). tapt DONE 022db4183 (smoke
identical, A/B Elves/Maze/Merfolk 0 change). Still running: wavebp (agent a833..., fd 0, smoke pending; then hatch
smoke, A/B, delete hatch, restore settings.local.json), mana (agent a099..., A/B + Cavern + delete levers + #22),
discard-bucket draft (agent acbc..., worktree elves-discard, flag MTG_ELVES_BUCKET_DISCARD default OFF; USER decision).
Unit gate (revised): default FULL suite + UNPRUNED/HUMAN_PLAY on touched test files only (full-suite UNPRUNED takes hours;
full HUMAN_PLAY segfaults with 414 failures on base too).

Next: cherry-pick wavebp + mana commits (only their own; based 4fc09f9b3) onto wip/elves-int -> build -> units ->
Elves fd sweep 0 -> smoke (expect Stompy/Selesnya moves from mana; classify, d8 b0 slower) -> ledger sweep flags 0 ->
tiers --deck=elves,elves2hg accept + check_gt_logs, commit rows+GT+suite_cost -> 5b, 5c2, scaling A/B, Stage 6 ->
push_queue join/land, CI watch. 2-hourly status cron 61a8eb73 (session-only).

## Stage 6 -- report (2026-11-26, landed 3f91b7d4a on phase-1-2-deck-analyzer)

1. **Cards implemented:** Joraga Warcaller (T3, multikicker faces K 1..20, per-counter lord), Galadhrim Brigade
   (T3, squad token copies, each a lord), Immaculate Magistrate (T2, N-counter tap-target pump), Allosaurus Shepherd
   (T2, layer-7b team base 5/5, new `TeamBasePT` mode), Elvish Clancaller (T2, lord + named TapTutorPut), Leaf-Crowned
   Visionary (T2, optional {G} draw on Elf cast), Marwyn (T2, power-scaled dork + enter counters), Taunting Elf (T1,
   lure inert, USER-approved); Concordant Crossroads gained the World rule (CR 704.5m). Coverage 18/18 full.
2. **Mulligan profile:** none yet (default settings; `Elves.profile.json` holds only the analyzer's defaults). The
   deck is now in all three suite tiers, so `mullgen.sh run decks/Elves` is unblocked (needs the memory fix
   ddc45ce91, which is in the next landing).
3. **Win turn** (accepted GT, d5 b20 play settings): overnight d5 4.32-4.36 (300-game rows), regression d5 4.38-4.40,
   2HG d5 4.75-4.83; d3 4.33-4.40; d0 4.89-4.90. ~4 non-wins per 1,260 d5 games, all land screws.
4. **Verification:** `verify_deck.py` GATE PASS; fd/nonconv 0; multi-depth monotone; claude-play 18 games (1 faster,
   17 equal, 0 slower; 5 flags, all fixed: fd #20, ContPins #21, Genesis Wave site-10 put continuation, Magistrate
   same-plan target, mana cluster #22/#23); viewer surface clean (dig, dragon, firebreathe, target + kick-count
   cycling); 5c2 tie-break neutral (default kept); discard draft does not move play (recommend not adopting).
   Found and fixed on the way: a lethal-projection under-read (same-plan lords pumping hasty casts; sacrificed mana
   creatures counted as attackers), and a single-step memory blow-up in rare Elves positions (next landing).
5. **6a disclosure** (read from the code, ElvesProvider : SelesnyaLifegainProvider):

| assumption / heuristic | source | class | cost / why safe |
|---|---|---|---|
| passive goldfish opponent (never blocks / casts / gains life); clairvoyant search; d5 b20 play settings | engine | global | Taunting Elf's lure and Shepherd's uncounterable clauses are inert under it |
| scaling-sink rule: one scaled sink per plan at its max minus hold-back counts for <=2 attacking 1-mana dorks; no partial splits (Wave X, Warcaller K, Brigade squad, Ezuri main pump) | `ElvesScalingAdmit` (USER rule) | PRUNING | held-out A/B net +0.0025/+0.0040 (std/2HG), 2 faster / 11 slower over 3,000 fresh-seed games, lines unfindable at d8 b0: it misses holding back SCALED dorks or a 3rd+ dork as attackers. Amended "hold-all" version measured net 0.0000 both formats (`wip/elves-scal`) -- USER decision |
| Genesis Wave X menu widened to every X, largest first | `ElvesProvider::XCandidates` | ordering/completeness | makes "Wave for what is left" exist; nothing removed |
| Genesis Wave put-all (every revealed permanent with MV <= X is put) | ElvesProvider put policy 948933d6c | correctness shortcut | every permanent in this pool is weakly better in play than milled; byte-identical to the engine default here |
| plan-dedup signature keys every {X} cast's X (`#XV<x>`) | engine (5ff9de56a; the Elves-only `PlanSignatureKeysWaveX` opt-in deleted) | completeness | every deck; A/B d5 b20: selesnyav2 / 2HG 2,000 games each net +0.0000, 0 slower; landfall byte-identical; +0.1% units |
| filter-before-axes (memory) | `PlanListFilterBeforeAxes` (landed 5194cedda) | identity | play byte-identical; only bounds memory |
| inherited SelesnyaLifegain hooks incl. its USER-settled discard buckets | base class | inherited | Elves' own draft moved 0/800 games; real cleanup discards ~1 per 400 games |
| multikick / squad faces capped at K=20 (USER 2026-11-27, lethal size; was 16) | card DB | model bound | 41 mana for a Warcaller x20 |
| Visionary unpinned beyond the horizon = pay when the {G} is spare (USER 2026-11-27; was decline) | `PayDrawPlayoutSpare` | playout policy | spare = the rest of the phase's line + plan activations + pre-combat pump count stay payable; a later phase's spends are not reserved; searched turns branch both ways |
| World-rule simultaneous-entry tie | engine | inert | Genesis Wave's put set is a choice |
| Cavern of Souls "choose a type" = any creature | card | exact here | every creature in the list is an Elf |
| viewer: no auto-resolved choice; Marwyn's tap is the mana-payment exemption | 2c-ter / 5h | -- | viewer-ready |

6. **Accepted deferrals:** Taunting Elf lure (USER-approved). PROVISIONAL (sign-off owed): Shepherd uncounterable /
   Dinosaur type inert; six INERT_PARAMS classifications in `audit_viewer_decisions.py`.
7. **Next steps:** the USER's scaling-rule amendment decision; landing ddc45ce91 (memory bound); then the mulligan
   profile (`mullgen.sh run decks/Elves`); the Wave-X dedup fix for Landfall / SelesnyaLifegain; re-testing
   `MTG_ATTACK_LORD_CAST` now that the sacrifice under-read is fixed (USER's call); the remaining under-reads in the
   lethal projection (same-plan haste-granting lords, double strike, Marwyn counters, replicate copies' lord share).

## Coordinator checkpoint 6 (2026-11-26 ~22:45 UTC, pre-compaction) -- RESUME HERE

LANDED 3f91b7d4a (Elves in all 3 tiers + GT, every fix; Windows CI 45895290416 success). Now at the push-queue HEAD
again with batch 2 = 871f02be1 memory bound (`PlanListFilterBeforeAxes`, play-neutral) + Stage 6 ledger, rebased on
91ef541bb; `push_queue.py hold -- build/scratch/elves-int/postrebase1.sh` running (build -> units -> smoke; smoke must
be 0 changed vs GT). Then: check_gt_logs, `push_queue.py land`, CI watch (--interval 60).
Running agents: wip/wavex-sig (Genesis Wave X signature completeness for every deck; A/B running), wip/bpput-cost
(sound fold for site-10 put continuation cost, 1.2-1.45x on ramp decks). USER decisions open: scaling-rule
"hold-all" amendment (wip/elves-scal 8de45a937, net 0.0000 both formats vs prune-off; current rule +0.0025/+0.0040,
lines unfindable at d8 b0); discard draft (recommend not adopting); MTG_ATTACK_LORD_CAST re-test; PROVISIONALs.
Mulligan profile deferred until the hold-all decision (Rule 0: no pending play change). Cron 61a8eb73 still armed.

## Coordinator checkpoint 7 (2026-11-26 ~23:40 UTC) -- RESUME HERE

LANDED 5194cedda (batch 2: memory bound + Stage 6 + checkpoint 6; units 4310/4310, smoke 251/251 0 changed; CI
45907751071 watched). Batch 3 on wip/elves-int: 5ff9de56a Wave-X dedup completeness for every deck (wip/wavex-sig
30208dee7; units 4311/4311 + smoke 251/251 0 changed on the integrated tree) and e6cca73e1 site-10 inert-land fold
(wip/bpput-cost 54756c7dc; sound identity fold, default ON; units/game 1.000 on four ramp decks, 0.997 hydras --
the put continuation's cost is real new mana / creatures, which no sound fold may remove; widening candidates
listed in docs/design/bpput-inert-land-fold.md each need their own proof). Next: combined build -> units -> smoke
(0 changed expected), push_queue join/land, CI watch. Both agents are finished. USER decisions open: unchanged
from checkpoint 6.

## Coordinator checkpoint 8 (2026-11-27 ~01:10 UTC) -- RESUME HERE

LANDED a35c0ccc0 (batch 3: Wave-X dedup for every deck + inert-land fold; units 4314/4314, smoke 251/251 0 changed;
CI 45916568355 watched; batch 2 CI 45907751071 success). All agents finished.
Mulligan profile STARTED (the hold-all scaling decision is still the USER's; the table is fitted to the USER's
current rule, which stands until answered -- an artifact fitted to an older engine is fine per the USER ruling).
Feasibility: discovery K=17 (only Llanowar Elves + Elvish Mystic merge; no bucket ruling = K as discovered),
~245K hands. 0e62f43e8: overnight rows ENTRY->FULL (d3 s4004 75, d5 4x425; Landfall's shape) accepted --deck=elves
with the profile absent (only the 5 resized keys moved; 0 overlapping games changed).
Running: `flock heavy.lock bash scripts/mullgen.sh run decks/Elves` (complete), log
build/scratch/elves-int/mullgen.log, harness task bbtiwovyl; status `bash scripts/mullgen.sh status decks/Elves`.
Then: on VALIDATION PASSED commit the keep table (.keepmodel.exhaustive.profile.json.gz + .raw.json.gz +
profile.json gen settings), re-run Elves smoke/regression/overnight rows with the table (keep_adoption_attribution),
accept --deck=elves,elves2hg per tier, check_gt_logs, push_queue join/land, CI watch. Status cron 0ff5efb4
(replaces 61a8eb73).

## Coordinator checkpoint 9 (2026-11-27 ~01:25 UTC, pre-compaction) -- RESUME HERE

USER asked for the viewer to create Elves references BEFORE the mulligan generation gets far, so the mullgen run
was STOPPED during its PGO build (nothing lost; no journal). Primary checkout: untracked CRLF decks/Elves/Elves.cod
(identical modulo CRLF) moved to logs/elves-original/Elves.cod.crlf-original; `git pull --rebase` running (task
bhw3gvbdn). Next: confirm the pull, check_references_safe, restart the viewer FROM THE PRIMARY CHECKOUT ONLY
(`MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups ./play.sh --no-open`, after killing the old
worldfire viewer pid 384685 once its tree has no uncommitted refs), give the USER the URL, commit + push every new
references/Elves file at once. Relaunch mullgen (checkpoint 8 command) only once the USER is done with references
and any misplays they reveal are fixed.

## Coordinator checkpoint 10 (2026-11-27 ~05:20 UTC, pre-compaction) -- RESUME HERE

* LANDED 90903dfb9 (viewer kick-count dialog + `multikick_max` engine field; Elves overnight GT resize; ledger 7-9). CI
  45937052063 watch = harness task b4ecim311. Primary checkout at 90903dfb9, Release rebuilt 04:11 (has multikick_max);
  viewer running from the PRIMARY (task bf3j5j2uk, logs/elves-viewer.log); a page reload gets the dialog.
* USER references: references/Elves/claude_s{1..4}_gi{0..3}.json all pushed (last 1155c6596). ref_bench elves: human 4.75,
  search 4.25, 0/4 short. Watcher: `bash build/scratch/refcommit.sh` (commits+pushes, then waits; re-arm after each fire).
  The 01:20 viewer backup failure was 9p ENOMEM under 5 concurrent builds; every build now goes through heavy.lock.
* USER rulings 2026-11-27 (verbatim): discard -- "We should have a discard policy that just buckets mana and threats. (mana
  elves can go in the mana bucket) It doesn't need to be complicated." + "We want a lot of mana in this deck, so we can
  feel free to add more mana as long as we have 1-2 scaling threats to take advantage of it. If not, I suppose we need
  more threats. Even small mana-producing elves can be useful here as a mix of producer and attacker."; hold-all --
  "Why would we want to hold back all of our mana dorks? ... maybe we should use a heuristic to be smarter about this? I
  suspect the benefit you are finding are in some specific situations?"; lord cast -- "I suppose we could try it."; "I
  think Kick counts should be capped at lethal size of 20. Leaf crowned-visionary should probably not decline the draw
  automatically even beyond the search horizon" + "If you don't have anything else to use the mana for it should draw.";
  viewer -- "It's probably better just to pop up a dialog." (done); Maze -- "If you run out of work to do on elves here
  maybe try to run the mulligan profile for Maze's End with K=20?"
* Branches (all wip, pushed by agents; integrate + validate + land as ONE Elves play batch):
  - wip/elves-disc2 657a68cf6 + 7b7f9b1e7: USER discard rule DONE, default ON, units 4321, A/B 0/1600 moved.
  - wip/elves-tweaks a08f4165f + fad30d94b: kick cap 20 + Visionary playout pay-when-spare; agent running smoke then
    A/B (temporary selector MTG_PLAYOUT_PAY_DRAW to delete).
  - wip/elves-scal2: smarter hold-back (lump points / sink-aware) vs holdall; round 1 done, round 2 (15k jobs) queued;
    agent at ~240K (handoff due). USER decision on the band after it reports.
  - wip/elves-lordcast c82aa6cff: MTG_ATTACK_LORD_CAST still FAILS (Boars d0 +151; Elves play +6 / 2HG +1, gi663
    unrecoverable at d8 b0); round 2 (hand fix + verify arms) running ~04:40 -> ~06:40 UTC; default stays OFF (USER).
* Maze's End K=20 mullgen prepared: worktree .claude/worktrees/maze-gen2 (wip/maze-gen2 = origin + the 3 USER bucket
  commits cb335d1f8/0965fdd9d/502c2ac4c). Start when the Elves heavy runs are done:
  `cd .claude/worktrees/maze-gen2 && MTG_RSS_CAP_GB=20 flock /workspaces/MagicDeckTester/build/heavy.lock bash scripts/mullgen.sh run "decks/Maze's End"`
  (harness-tracked, run_in_background); confirm discovery K=20; heartbeat in 10 min; append slow-generation-jobs.md.
* Elves mulligan profile: still paused (USER's hold-back decision + the play batch must land first).
* Pre-existing defect seen: test/viewer_client_check.js "no silent revert" fails 3 checks on origin too (replay drift
  to a tutor_etb decision) -- not yet assigned.
* Cron b3381ea5 (2-hourly, session-only) armed.

## Coordinator checkpoint 11 (2026-11-27 ~09:00 UTC) -- RESUME HERE

USER 2026-11-27 ~07:15: "Okay, I finished the rest of seeds 1-10 in terms of references. You should check they are
working, finish up anything else remaining and then proceed with the mulligan profile."
* References: references/Elves/claude_s1_gi0 .. claude_s10_gi9 all committed + pushed (last e1570da74). Strict
  viewer_protocol_check on all 10: 10 ok, 0 drift / enum-gap / board-diverged. ref_bench: human 4.50, search 4.30,
  0/10 short, search 1 turn faster on s3 and s4.
* Play batch on wip/elves-int (rebased onto origin e1570da74, which carries Dragonstorm APPLY-DROP dea141a1e):
  c39ff9e5d+2c6a12975 discard buckets (default ON, USER rule; 0/1600 paired games move);
  5e8e09120 holdall scaling band (round 2 of the smarter rules: lump / sink-aware equal holdall on win turns on fresh
  seeds; lump saves ~0.3% 2HG units only; holdall integrated as the recommended default, USER decision open);
  85e09656e+bf58ca704+dbcefd4ed+887a691a6 kick cap 20 + Visionary spare-{G} draw beyond the horizon, amended so a
  pre-combat {G} never taps an attack-ready creature (the USER's "if you don't have anything else to use the mana
  for": attacking is a use). Amendment A/B: d0 -0.0085 (6 slower / 23 faster), std/2HG d5 b20 0/0. No selector left.
* Pre-rebase validation (78baac178): units 4327/4327; smoke + regression moved only elves/elves2hg keys, searched 0
  slower / 3 faster, d0 net faster; 964 refs replay with 0 drift. GT commit dropped at the rebase (GT conflict with
  the Dragonstorm accept) and is being REGENERATED under the rebased binary: push_queue hold -> heavy.lock ->
  build/scratch/elves-int/validate.sh (status validate.status, DONE marker validate.DONE). Lease is ours.
* Then: accept --deck=elves,elves2hg (smoke + regression), check_gt_logs, commit, `push_queue.py land`, CI watch;
  then START the Elves mullgen from this worktree: `MTG_RSS_CAP_GB=20 flock heavy.lock bash scripts/mullgen.sh run
  decks/Elves` (complete recipe, discovery K=17 expected, no journal exists), harness-tracked, heartbeat in 10 min,
  2-hourly durable:false cron, append docs/design/slow-generation-jobs.md. Maze's End K=20 (wip/maze-gen2) follows it.
* MTG_ATTACK_LORD_CAST round 2 (wip/elves-lordcast): fails the bar (Boars d0 +163, Goblins d0 +49, Elves +3, 2HG +8,
  2 unrecoverable at d8 b0); stays OFF pending the USER. Its temp selectors live only on that wip branch.
* OPEN USER QUESTIONS: holdall vs holdall+lump; do two Galadhrim Brigades count as a split (2HG 814, unfindable at
  d8 b0 under the no-split rule); MTG_ATTACK_LORD_CAST off; the 4 discard-rule readings; Shepherd / Ezuri inert
  clauses + six INERT_PARAMS sign-offs; kick cap 20 vs 2HG's 30 life.

## Coordinator checkpoint 12 (2026-11-27 ~09:35 UTC)
LANDED f2e307fe3 (Elves play batch: discard buckets ON, holdall, kick cap 20, Visionary beyond-horizon draw with
the pre-combat attacker amendment; GT accepted smoke+regression, 0 searched slower, only elves/elves2hg keys move;
check_gt_logs clean). CI run 45973934344 watched.
Mulligan profile RUNNING: `MTG_RSS_CAP_GB=20 flock heavy.lock bash scripts/mullgen.sh run decks/Elves fast`
(harness task b2lox12h6; log build/scratch/elves-int/mullgen2.log; rc marker build/scratch/elves-int/mullgen.launch).
RECIPE = **fast** (R30): the USER default (2026-10-19) -- no instruction for complete, and K=17 / ~245K hands is not
clearly cheap. (Checkpoint 8's "(complete)" label was wrong: its command passed no recipe, so it was fast too.)
Status cron d1bc103d (replaces b3381ea5). After: commit the keep table, re-run Elves rows with it, accept, land;
then Maze's End K=20 mullgen from .claude/worktrees/maze-gen2.

## USER answers 2026-11-27 ~09:50 UTC (verbatim) and what follows from each
1. Hold-back: "Hold all dorks back is okay, but not sufficient in my mind unless we have heuristically determined the
   right option in our situation. i.e. If just searching we should probably consider all, hold back normal dorks and
   hold back all dorks. If we add some heuristic logic to figure out the win turn we could potentially prune that
   further." -> the landed band already admits every count from full scale down to all-dorks-held (a superset of the
   three options); agent (wip/elves-r2) builds the three-point narrowing behind MTG_ELVES_HOLDBACK_MODE and A/Bs it.
   Default stays the band until the USER decides.
2. Brigade: "Why would you play 2 Galadhrim Brigades when you could play one and Squad it? I guess the only case I can
   think of would be if we played it for three and gained mana as a result, but it seems to me like the other option
   would almost always win out." -> the no-split rule stands; game 814's mechanism is being explained (wip/elves-lordcast).
3. Lordcast: "I would leave it off for now, but we probably should figure out why it doesn't work." -> OFF; root-cause
   investigation running (wip/elves-lordcast).
4. Discard: "I don't understand the Taunting Elf comment. Genesis Wave is technically MV 3, but realistically needs to
   be cast for 6 mana or preferably even more to be good. It shouldn't be counted as a cheap spell in the discard
   heuristic. Mana Elves should indeed be kept above spare lands. We ideally do want to play a land next turn, though,
   so keeping one is nice if we can manage it." -> Wave priced at 6 in the discard heuristic; one land always kept as
   the last mana card shed (the <4-lands condition dropped); additive floor reading not contested (wip/elves-r2).
5. Sign-offs: "Yes, I thought I signed off on these already." -> Shepherd uncounterable / Dinosaur clauses, Ezuri
   regenerate / trample, and the six INERT_PARAMS are USER-APPROVED (no longer provisional).
6. Kick cap: "Yeah, we may want to scale it up for 2HG arguably." -> cap = the opponent's starting life (20 / 30),
   wip/elves-r2.
7. Hold-back follow-ups (same session): "We could search the options in-between, but that seems quite costly." /
   "If we don't need a prune then we don't need a prune. Though we should check how much this particular branch costs
   and make a decision on whether we perhaps do want a prune. My thought was a bit rough, so if we actually need to
   prune this then perhaps we should take the "heuristic calculation" approach to set things up for lethal as soon as
   possible instead." / "This is also a tricky problem, because technically on earlier turns when we are setting up,
   you usually want to use the scaling elf mana last, to get more on board before you tap it. Then in late turns you
   want to work the other way around to leave multiple attackers." -> the payment ladder ranks a scaled dork tap-LAST
   on every turn (DorkGrowth rank 61); wip/elves-r2 measures how often the order matters on attack turns and
   prototypes a searched scaled-first / scaled-last payment choice (default OFF) or a lethal-aware order.
8. Sink turns: "That said, when using a scaling spell, (Joraga, Galadhrim, Genesis Wave) you usually would want to
   prioritize attackers or just pump all of the mana into the spell." -> candidate for sink turns = two options,
   all-in (class top) or attackers-first (scaled dorks pay first, fixed attack-capable dorks held); the set-up
   tap-last order applies to the non-sink casts. wip/elves-r2 adds a `two` arm and counts how often the band's
   winning count is neither.
