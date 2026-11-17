# Analysis ledger — Red Elementals

Deck: `decks/Red Elementals/Red Elementals.cod` (mono-red Elemental aggro, 60 cards): 4 Hellspark
Elemental, 4 Spark Elemental, 4 Flamekin Bladewhirl, 4 Ball Lightning, 4 Incandescent Soulstoke,
4 Blistering Firecat, 2 Nova Chaser, 1 Sunspine Lynx, 4 Thunderkin Awakener, 2 Metallic Mimic,
4 Lightning Bolt, 4 Legion Leadership (MDFC spell // land); lands 15 Mountain, 4 Fiery Islet.

The user dropped the list into the primary checkout (untracked) and asked for an analysis on
2026-11-14. It was COPIED here (CRLF -> LF); the user's original stays in the primary checkout
untouched.

Branch/worktree: `redelem-analysis` @ `.claude/worktrees/redelem` (cut from `e944eeea4`).
Box: 12 threads (`nproc`), 10 GB RAM -- at most ONE compile at a time, never a compile beside two
batches, `MTG_BUILD_JOBS=6` for a build beside one batch.

**The deck's engine.** Hasty one-shot Elementals (Spark / Hellspark / Ball Lightning / Firecat, all
sacrificed at end step) re-bought from the graveyard: Hellspark's unearth, Thunderkin Awakener's
attack trigger (an Elemental with toughness < 2 from the graveyard, tapped and attacking).
Incandescent Soulstoke is a lord and puts an Elemental from hand in with haste. Nova Chaser (10/2
trample) champions an Elemental -- championing an attacker that would otherwise be sacrificed at end
step saves it (a post-combat main line). Legion Leadership doubles power. Metallic Mimic names
Elemental. Flamekin Bladewhirl reveals an Elemental or pays {3}. Sunspine Lynx pings each player per
nonbasic land (our own Fiery Islets / Legion Leadership lands hit US).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, blocks or attacks; it has NO lands, artifacts or
  enchantments; in 8 of every 10 games it gets passive creatures on a fixed schedule
  (`GoldFishRunner::PopulateOpponentSpawns`) that exist only as removal targets. Opponent-facing
  clauses are often inert, but self-binding clauses bind us. Model the full card; goldfish-inert
  deferrals are PROVISIONAL until the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold -- and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic): "an Elemental" is a subtype param, never a name.
- No unbudgeted-only changes; booleans via `EnvOn`; scope a change by where it is valid, never by what
  keeps GT still.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- Agents: no `timeout`, no `rm -rf` / `git checkout --` / `git stash` (bare), no pushing the shared
  branch (the parent pushes). Commit on `redelem-analysis` in the worktree. Hand off at ~250K context.

## Stage 1 — coverage (2026-11-14)
Missing (7): Hellspark Elemental, Spark Elemental, Flamekin Bladewhirl, Ball Lightning, Nova Chaser,
Sunspine Lynx, Thunderkin Awakener.
Present (7): Incandescent Soulstoke, Blistering Firecat, Lightning Bolt, Mountain, Fiery Islet,
Legion Leadership, Metallic Mimic (all "full"; bracket notes re-checked for this deck at Stage 3).
No fetchlands; no sideboard.

## Stage 2 — cards
| card | tier | mechanism | status |
|---|---|---|---|
| Spark Elemental | 1 | keywords Trample/Haste + `endstep_sacrifice_self` (Firecat shape) | done (agent A, e313899fc) |
| Ball Lightning | 1 | as Spark | done (agent A, e313899fc) |
| Flamekin Bladewhirl | 1 | `reveal_or_pay_subtype` Elemental + `reveal_or_pay_cost` {3} (printed subtype; one own copy skipped; Mimic in hand is not an Elemental) | done (agent A, e313899fc) |
| Sunspine Lynx | 2 | `etb_damage_each_player_per_nonbasic_land` (shared ResolveNonbasicLandDamage in the own-ETB cascade; arms own_death_live) + `static_players_cant_gain_life` / `static_damage_cant_be_prevented` (PlayersCantGainLifeNow / DamageCantBePreventedNow at the Skullcrack sites; `deck_has_static_life_locks` stamp) | done (agent A, e313899fc) |
| Hellspark Elemental | 3 | `endstep_sacrifice_self` + `unearth_cost` {1}{R}: NEW `Action::Kind::Unearth` (graveyard activation, one per copy, equiv_tag prefix fold), `ApplyUnearth`, `Permanent::unearthed` (end-step exile sweep + exile-instead at SacrificePermanentAt), `unearth=` viewer verb | done (agent A, e313899fc); replacement PARTIAL (see open questions) |
| Nova Chaser | 3 | CHAMPION (new, generic `champion_subtype`): searched exile pick (cast + Soulstoke-put variants per identity class + decline), linked exile, LTB return as a new object; `DeckUsesSecondMain` opened for the post-combat champion line | done (agent B, see Agent B notes) |
| Thunderkin Awakener | 3 | NEW generic `attack_trigger_reanimate` (+ `_subtype` Elemental, `_toughness_lt_self`, `_sac_end_step`): shared `FireAttackReanimate` in both combat worlds, SEARCHED target (`Plan::attack_reanimate_choice` -> `scripted_attack_reanimate`, one rank per distinct name, a returned Nova expanded per champion pick), tapped + attacking through the enter cascade, delayed end-step sac on the object, projection `CountAttackReanimatePump`; viewer `gy_return_one` (mandatory) | done (agent C, f409f2e07; validated agent C2, see Agent C notes) |

### Agent A notes (2026-11-14) — Spark / Ball Lightning / Bladewhirl / Lynx / Hellspark
- **Correction to the standing decisions:** the passive opponent DOES have lands -- three inert
  placeholders, 0-3 of them `Opponent Nonbasic Land` by game index (`GoldFishRunner`, kNonbasics). So
  Sunspine Lynx's ETB deals 0-3 to the opponent as well as 1 per Fiery Islet / Legion Stronghold to us.
- Unearth structures (for the Nova Chaser / Thunderkin agents): `CardParams::unearth_cost`;
  `Action::Kind::Unearth` (card_name = gy card, hand_index -1, sac_source_id = that copy's m_number,
  trailing pass, applied AFTER the plan's casts); `UnearthGraveyardIndex` / `ApplyUnearth`
  (SpellEffects.h; enters via `PutCardOntoBattlefield(..., unearthed=true)` under GraveyardEnterScope,
  sets `Permanent::unearthed` + `temp_haste` BEFORE the enter cascade); `PerformEndStepUnearthExiles`
  at the top of both end-step sites (before the delayed / self sacrifices); the leave replacement is in
  `SacrificePermanentAt` (exile, sacrifice watchers fire, no dies triggers) via
  `RemoveUnearthedToExile` + `UnearthedLeftBattlefield`. `unearthed` is per OBJECT: a champion exile
  that later returns the card returns a new Permanent with the bit clear (falls out). A champion that
  exiles an unearthed creature must move it with its OWN exile (not SacrificePermanentAt). Graveyard
  readers: unearth sets `GyR_CreatureNames` (Thunderkin's gy reanimation reads creature cards too).
- Second main: unearth is offered in every main phase (sorcery speed); post-combat it carries eval -2
  and no attack stamp. `DeckUsesSecondMain` NOT extended -- nothing real needs a post-combat unearth
  (the only candidate line, championing an unearthed body with a post-combat Nova Chaser, is the
  Champion agent's call).
- Validation: unit `test/unit/test_red_elementals.cpp`; repro `--seed 1 --games 8 --depth 3
  --budget-ms 200` game 4 (T4: Mimic, unearth Hellspark, attacks for 4 with the Mimic counter; never
  back in the graveyard); `--seed 100 --games 40 --budget-ms 100`: 10 unearths (all main 1),
  Bladewhirl 28x revealed {R} / 1x paid {3}{R} (hand of Mountains), Lynx T4 (us 18->14 incl. Islet
  pain, opp 17->14 off 3 placeholders). Claude-play seed 5 T3 offers the unearth plan (`verb:
  unearth`), `--validate-line "land=Mountain;unearth=Hellspark Elemental"` -> accept. Byte-identity:
  Morph / Landfall Elementals / Prevent Damage / Critter Lifegain 6 games each at d3 b40, base vs new
  logs identical.
- `test_pirates_provider` "every deck in decks/ resolves" FAILS until the Red Elementals folder gets
  its routing row (Stage 4a; endstep_sacrifice_self is also a Morph-provider signature param).

### Agent B notes (2026-11-14) — Nova Chaser / CHAMPION
- **Param:** `CardParams::champion_subtype` ("Elemental"). Inert for every card without it.
- **Pick plumbing (searched, never heuristic in the window):** `ChampionVariantTargets` (SpellEffects.h)
  = one battlefield m_number per identity class (SOUND fold: `LandfallTargetFoldKey(..., mix_doom=true)`,
  which KEYS temp haste / sac_at_end_step / unearthed / exile_at_end / destroy_at_end / attack count
  instead of refusing them). CollectActions emits one `CastFromHand` per class (`Action::enchant_target`
  = that m_number, the Protege precedent) + a decline (`-1`, eval 0). The unpinned ordinary cast
  (`enchant_target 0` -> `DecisionProvider::OrderChampionCandidates` front AT RESOLUTION) is kept only
  when `ChampionSamePlanEntrantPossible` (an Elemental creature card / a Mimic in hand) -- otherwise it
  would duplicate the front variant and is skipped. The Soulstoke `PutFromHandAbility` gets the same
  variants. Both worlds install the pick as `g_champion_pin` (`ChampionPinScope`): EffectHandler
  `EnterBattlefield`, `apply_one`, and `ApplyPutFromHand(..., champion_pick)` (new trailing param; the 4
  call sites pass `a.enchant_target`). Human play (`HumanPlayActive`) gets no variants: the `dig`
  "(champion)" modal asks (main.cpp note + `index.html` branch; `-1` = exile nothing, the champion is
  sacrificed). Registered in `audit_viewer_decisions.py` MANIFEST (`champion_subtype: dig`).
- **Resolution:** `ResolveChampionEtb` runs at the END of the `FireOwnEtbTriggers` wrapper (after the
  entrant's other own ETBs, so no early return skips it and no stale index is read). Exile = own removal
  (detach, erase, card to exile with marker `kChampionExileBase - (2*champion + owner)`, champion's
  `linked_exile_number`), then `UnearthedLeftBattlefield` (LTB triggers + refreshes; no dies, no
  sacrifice watchers). A token ceases to exist. Decline / no candidate -> `SacrificePermanentAt`.
- **Return:** `ReturnChampionedCard` from `FireLeavesBattlefieldTriggers` -> `PutCardOntoBattlefield`
  (new object: sick, enter cascade, Mimic counter, no unearth / sac_at_end_step).
- **End-step timing (CR 603.2), new:** `g_end_step_window` opens in `PerformEndStepUnearthExiles` and
  closes in `PerformEndStepSelfSacrifices`, which skips cards that RETURNED during the sweeps
  (`g_end_step_late_entrants`): a Soulstoke-put Nova sacrificed by its delayed trigger returns e.g. a
  Spark that survives our end step (it dies at the opponent's). **Thunderkin agent:** a Thunderkin-
  returned body that is championed and later returns is a new object -- your delayed sacrifice must key
  on the object (a `sac_at_end_step` bit on the Permanent, like Soulstoke's) and will then correctly find
  nothing. Nova (toughness 2) is never a Thunderkin target if Thunderkin's toughness is 2.
- **Attack projection:** the Path-to-Exile self-target bit block also marks a champion variant's exiled
  creature as out of this turn's attack.
- **Validation:** unit (8 new cases in `test/unit/test_red_elementals.cpp`: chosen exile linked; no
  candidate / decline sacrifices; Nova champions another Nova; Mimic counts; LTB return new object;
  unearthed Hellspark returns free of unearth; post-combat champion survives end step + enumeration
  offers per-candidate + decline and no duplicate; mid-end-step return misses the trigger; identical
  doomed attackers fold). Full suite 3070/3071 (only the pre-existing pirates routing case). Scenario
  `test/scenarios/redelem_nova_postcombat_champion.json` (opp 21: Spark attacks, MAIN-2 Nova champions it,
  win T5) PASS at b0 and b100. Repros d3 b100: seed 100 x40 (avg 4.85) and seed 300 x120 (avg 4.65):
  Nova lines seen -- Soulstoke-put Nova championing the tapped Soulstoke (6x), Nova championing
  Bladewhirl / Mimic pre-combat, and seed 300 gi117 T5 MAIN 2: unmorphed Firecat attacks for 7, Nova cast
  post-combat champions it, Nova kills T6 (line (a) found by the search). Claude-play seed 101 choices
  `1,0,0,0,1,0,0,0,0,0,1` surfaces `dig` "Nova Chaser (champion)" over [Bladewhirl, Metallic Mimic];
  answering 1 exiles the Mimic. Byte-identity (base vs new, 8 games each, seed 4242 d3 b40): Landfall
  Elementals, Morph, Goblins, Spirits (the last three search a second main) -- 32/32 logs identical.
- **Observation (tie, not a loss):** seed 300 gi83/gi108 cast Nova T4 on an Elemental-less board (it is
  sacrificed) while still winning T5 -- the unpinned cast (kept because an Elemental is in hand) carries
  the 10/2's eval; every line tied on win turn.

### Agent B2/B3 notes (2026-11-14) — champion in-window picks, validation
Code: `b53c6f21c`. It SUPERSEDES the "unpinned ordinary cast ... kept only when
`ChampionSamePlanEntrantPossible`" sentence above: that variant and that function no longer exist.
- **Searched shapes (B2).** Cast menu = one variant per board identity class (provider order) + one per
  SAME-LINE entrant NAME (`ChampionSameLineEntrants`: an Elemental creature card / a Mimic in hand;
  `enchant_target` = that card's number, `trick_hand_target` = its name) + the decline.
  `SubsetHasMissingTrickTarget` requires the entrant's cast and `ApplyAttachSpellDependencyOrder` orders the
  champion after it. Soulstoke put menu = board classes + hand names + graveyard unearth names + decline
  (`DeferSameLineSacs` / `HasSameLineTrailingSac` / `TrailingRunsBefore` run a pinned put after the
  others). Resolution `ChampionResolvePin`: the exact number, else the newest legal permanent of that name (a
  by-name cast swapped the copy), else the decline. A PUT Nova championing a Nova gets one variant per
  return rank (`ChampionReturnRankBound`, packed `enchant_target = pick | ret<<21`; `ret` is kept in the exile
  marker and `linked_exile_number`, which is the state-key fold); `ReturnChampionedCard` installs the rank pin.
  Census `MTG_CHAMPION_AXIS_STATS` (TurnSolver.cpp `championcensus`). Provider hooks:
  `OrderChampionCandidates` (orders) and `PruneChampionCandidates` (`MTG_CHAMPION_DOOMED_PRUNE`, default
  ON; a doomed and spent candidate is admitted alone, with no decline and no same-line entrants;
  `MTG_UNPRUNE=champion` and human play get the full set). Both moved to `RedElementalsProvider` at Stage 4a.
- **Repro, new vs pre (B3)**, frozen `b53c6f21c` binary vs `mtg.pre`, seed 300 x120 d3 b100: avg **4.6500 ->
  4.6500**, 0 win turns differ (0 faster, 0 slower). Game logs: 118/120 byte-identical (except `runId`). The
  two that changed are gi83 and gi108, the defect-(2) games where the old T4 main-1 Nova was cast onto a
  board with no Elemental and sacrificed through the unpinned variant. gi108 now casts Spark + Bladewhirl,
  unearths Hellspark and attacks for 6 on T4. gi83 still casts Nova onto an Elemental-less board, but now
  in T4 MAIN 2, as the searched DECLINE variant, and Nova goes to the graveyard. That is a tie the search breaks toward a pointless cast (every line wins T5),
  not an unpinned pick. Both still win T5. Census: `searched-frame=0 playout=0 outside-window=0 real=0`.
- **Prune A/B (B3)**, default vs `MTG_UNPRUNE=champion`, d3 b100, frozen binary, one `--batch` per arm
  (both levers are process-wide env, not per-job `flags`). Seeds 300 x120, 100 x120, plus held-out 5000 x600:
  avgs 4.6500 / 4.6333 / 4.7267 on **both** arms. Per-game digests are identical in 840/840 games, so 0
  faster and 0 slower. Search units: s300 1,630,112 vs 1,630,115 (the prune saves 3), s100 1,142,943 on
  both, s5000 7,146,303 on both. The scenario `redelem_nova_postcombat_champion` gives identical counters
  under both arms too. So at this setting the prune is **inert in outcome and in work**. The doomed-and-spent
  shape (a post-combat Nova over an attacked Spark / Ball Lightning / unearthed Hellspark) is rare: 5 Nova
  casts (2 in main 2) and 9 Soulstoke Nova puts (main 1, championing the tapped Soulstoke, which is not
  doomed) in 120 games. The census reads 0 unpinned picks on every arm.
- **Byte-identity (B3)**, `mtg.pre` + `cards.base.json` vs frozen + its `cards.json`, 8 games seed 4242 d3 b40
  with `--ignore-play-profile` (Landfall Elementals and Goblins otherwise refuse `--depth` because their
  value_play depth is enabled). Landfall Elementals, Morph, Goblins, Spirits: **32/32 logs identical** apart
  from `runId`, and the stderr search counters are identical too.

### Agent C notes (2026-11-14) — Thunderkin Awakener
Code: `f409f2e07` (implementation, 5 unit cases, scenario); `05b603a2e` (viewer INERT_PARAMS rows, pending
user OK). Validation by agent C2 on the Release build of `f409f2e07`.
- **Structures.** Params (`CardDatabase`): `attack_trigger_reanimate` (gate) + `_subtype` ("Elemental"),
  `_toughness_lt_self`, `_sac_end_step`. `GameState::scripted_attack_reanimate` (packed pin) and
  `deck_has_attack_reanimate` (stamp, GoldFishRunner; also feeds `GyR_CreatureNames`), both in existing
  padding holes (sizeof 1464 unchanged). `Plan::attack_reanimate_choice` (4 bits x 7 trigger ordinals, ranks
  into `AttackReanimateOptions`; Plan size 656 unchanged), plumbed at every `attack_attach_choice` site
  (hash, SamePlan, empty-plan, axis-variant, combat-pin dedup, scalar plan-cache key, census, dumps, post-apply
  pins, Dominance fold, turn-start resets). `SpellEffects.h`: `AttackReanimateFilter`,
  `AttackReanimateOptions`, `ReturnGraveyardCardAttacking`, `FireAttackReanimate` (targets fixed as the
  trigger goes on the stack, CR 603.3d; re-checked on resolution against the live toughness or LKI; atk_idx
  repaired by number), called after `FireMilitiaPrideTokens` in `GameEngine::CombatPhase` and
  `SimulateOneCombatAfterTitan`. `PutCardOntoBattlefield` gained `tapped` and `sac_at_end_step`. Projection
  `CountAttackReanimatePump` (TurnSolver, copy-and-diff). Axis block "SEARCHED THUNDERKIN AWAKENER ATTACK
  RETURN" (TRACE `thunderkinaxis`). Provider `DecisionProvider::AttackReanimateTargets` ORDERS only (printed
  power desc, then a card without a graveyard ability first); no prune.
- **Unit suite:** 3078/3079 cases pass; the one failure is the known `test_pirates_provider`
  every-deck-resolves table (54 folders vs 53 pinned, Stage 4a).
- **Scenarios:** `redelem_thunderkin_unearth_contest` (only unearth Hellspark + cast Thunderkin + the
  trigger returning SPARK makes 7) PASS win T3 at b0 and b100; `redelem_nova_postcombat_champion` PASS win T5
  at b0 and b100.
- **Repro, seed 300 x120 d3 b100** vs the pre-Thunderkin baseline (`b53c6f21c`, where the card was
  undefined): avg **4.6500 -> 4.3500**, 32 win turns differ, **32 faster, 0 slower** (net -36 turns; gi94
  7->4, gi96 7->5, gi97 6->4). Census `searched-frame=0 playout=0 outside-window=0 real=0`. Axis trace: 21,495
  frames, 4,068 emitted variants (70,672 in all, max 210 in one frame). Game logs: 91 returns over 120 games
  (Spark 49, Ball Lightning 20, Hellspark 21, Firecat 1), every one tapped; each returned body is in the
  graveyard at the next turn or the game ended (Hellspark 12/12 to the GRAVEYARD, never exile: a trigger
  return is not an unearth). Combat damage equals the tapped bodies' printed power in 71/88 combats with a
  return; the other 17 are Metallic Mimic counters, the Soulstoke lord and Legion Leadership pumps.
- **Claude-play:** seed 302 surfaces `gy_return_one` "Thunderkin Awakener (attack)", mandatory (empty
  `alt_text`), single-candidate at choices `1,0,0,0,7,0` and with the full legal list [Spark Elemental, Ball
  Lightning], default 2 = Ball Lightning (provider order), at `1,0,0,0,7,0,1,0,0,0,1,0,0,0`. The dump shows
  the attacking Thunderkin untapped: the engine taps attackers in `ResolveCombatDamage` (pre-existing,
  engine-wide; nothing in this list reads it).
- **Byte-identity** (`b3/ident_b3.sh`, `C_base` binary + cards.json vs new, 8 games seed 4242 d3 b40
  `--ignore-play-profile`): Landfall Elementals, Morph, Goblins, Spirits **32/32 logs identical** apart from
  `runId`; stderr search counters identical. The extra scalar plan-cache key slot causes no drift.
- **Audits:** `audit_card_costs.py` rc 0 (627/627 match); Thunderkin is in `scryfall_reference.json`;
  `audit_card_fields.py` rc 0; `--coverage-only` 0 missing, all 14 cards `full`; `audit_viewer_decisions.py
  --no-sweep` rc 0 after `05b603a2e`.

## Stage 4a — provider routing (2026-11-14, agents D / D2)
Code: `f0bf6f249` (amended from WIP `9c46fbcdd`).
- **Routing BEFORE: `MorphProvider`, by accident.** Spark / Ball Lightning / Hellspark / Firecat's
  `endstep_sacrifice_self`, Firecat's `morph_cost` and Legion Leadership's `trick_double_power` are all terms
  of the `morph` signature, which routes first. Morph's hooks encode the USER's rules for the Morph list and
  premises stated "for THIS list" (e.g. `EndStepSacFaceUpDeadAfterCombat` assumes nothing reads a creature
  dying or sitting in the graveyard, which Thunderkin's attack return and Nova's champion both do here), so
  the deck does NOT derive from it.
- **Routing AFTER: `RedElementalsProvider : DeckProvider`** (`src/ai/DecisionProviders.h`, after
  `LandfallElementalsProvider`; impls in `DecisionProviders.cpp` ~870-1005). Signature `redelem` (in
  `DetectDecisionProvider`) = Nova Chaser's `champion_subtype` || Thunderkin Awakener's
  `attack_trigger_reanimate` || Hellspark's `unearth_cost` on an ELEMENTAL card: three cards, params no other
  card in `cards.json` carries. It deliberately excludes Soulstoke's `tap_put_from_hand_*` and Metallic Mimic
  (Landfall Elementals runs both), the end-step-sacrifice bodies / Firecat / Legion Leadership (Morph),
  Bladewhirl's `reveal_or_pay_subtype` (Kithkin / Pirates carry it) and Sunspine Lynx (a red staple). Routed
  ABOVE `morph`. Unit: the every-deck-resolves table in `test/unit/test_pirates_provider.cpp` pins
  `Red Elementals -> RedElementals` (this also fixes the 54-vs-53 failure agent C recorded), and
  `test_red_elementals.cpp` checks that the deck routes there and still routes there with any one signature
  card cut. `provider_audit.py --check` rc 0 (Red Elementals is "NOT AUDITED": no profile.json yet).
- **Certificate: `NotAssessed`.** What would have to be checked: a winless-this-turn bound must price every
  hasty body still castable or Soulstoke-puttable this turn (Spark / Ball Lightning / Hellspark / Firecat, and
  unearth from the graveyard), each Thunderkin attack trigger's tapped-and-attacking return of an Elemental with
  toughness below the Awakener's, the Soulstoke +1/+1 lord, Metallic Mimic counters, Legion Leadership's power
  doubling on the largest attacker, trample, Lightning Bolt (3) and Sunspine Lynx's ETB.
- **Hooks moved.** The root `DecisionProvider` `OrderChampionCandidates` / `PruneChampionCandidates` /
  `AttackReanimateTargets` are now NEUTRAL (no-op); the deck rules moved verbatim into
  `RedElementalsProvider` (`MTG_CHAMPION_DOOMED_PRUNE` kept). No other deck carries champion or the
  Thunderkin trigger, so the root change is inert elsewhere (identity check below).
- **Nova Chaser main-2 rule (USER, verbatim: *"Note that Nova Chaser should be cast main 2 if it is hardcast,
  but it might also be dropped in main 1 via Incandescent Soulstoke."*).**
  `RedElementalsProvider::RestrictsCollectedActions` / `CollectedActionAllowed`: a `champion_subtype` creature
  with no haste access (`TrickHandRecipientCanAttackThisTurn`: printed haste, or a haste grant on our
  battlefield / in hand) is not offered as a PRE-combat `CastFromHand`. The Soulstoke put
  (`PutFromHandAbility`) is untouched in both mains. Lever `MTG_REDELEM_NOVA_MAIN2` (heurarm slot
  `REDELEM_NOVA_MAIN2`, per-job overridable, default ON); `MTG_UNPRUNE=championmain2` (new `UnprunedGate::
  ChampionMain2`) and human play keep the main-1 hardcast. A provider RESTRICTION, not a pick: everything
  admitted is still searched.
  **The line it gives up (disclosed):** a main-1 hardcast Nova that DECLINES to champion goes to the
  graveyard, and a Thunderkin Awakener pumped to toughness >= 3 (the Soulstoke lord or a Mimic counter) could
  return it tapped and attacking as a 10/2 trampler that same turn. The rule removes that line. In the A/B
  below no game is slower for it (it measured 1 game FASTER, s300 gi83 6 -> 5, against the arm with the rule
  off).
- **`OrderOpaqueCastsByRank` = true** (lever `MTG_REDELEM_OPAQUE_ORDER`, heurarm `REDELEM_OPAQUE_ORDER`,
  default ON). This is a **CAST-ORDER heuristic and USER-OWNED; the decision is PENDING the user.** It is ON
  by default only because it restores what the deck had on Morph. Root cause: the first A/B of the bare
  `DeckProvider` derivation was 8 games slower and 0 faster than the Morph baseline, all Legion Leadership
  lines, where the bare provider leaves the order-opaque cast set to the search (s5000 gi308 T4: Leadership
  then Spark = 8 damage, Spark then Leadership = 9; s300 gi49: the Spark + Leadership T4 kill was not found
  even at `--budget-ms 0`). A temporary diagnostic provider that forwarded each Morph hook by bit showed
  `OrderOpaqueCastsByRank` ALONE restores all 8; the other Morph hooks had no effect on outcome (the
  diagnostic code was reverted, never committed). It rank-sorts by the GENERIC `CastOrderRank`, so the doubler
  follows the bodies it doubles; Morph's own tiered `CastOrderRank` is NOT taken.

### Stage 4a A/B (d3 b100, `MTG_DUMP_WINS` per-game paired)
Base = `build/scratch/redelem/D_base` (HEAD `44154dd93`, routes to Morph). Final = `D_new2` (the Stage 4a
commit). `new` = the first derivation WITHOUT the opaque order (`D_new`). Final, `m2off2`
(`MTG_REDELEM_NOVA_MAIN2=false` as a per-job flag) pooled in ONE batch; `unp2`
(`MTG_UNPRUNE=champion,championmain2`, env-only, so a second batch run concurrently). Budgets are
deterministic units, so the wall `ms` of concurrent arms is not compared.

| arm | s300 x120 | s100 x120 | s5000 x600 | units s300 / s100 / s5000 |
|---|---|---|---|---|
| base (Morph) | 4.3500 | 4.2917 | 4.3817 | 1,645,169 / 1,036,797 / 7,004,235 |
| new (no opaque order) | 4.3667 | 4.3083 | 4.3883 | 2,514,946 / 1,594,804 / 10,947,090 |
| **final** | **4.3500** | **4.2917** | **4.3817** | **2,435,831 / 1,587,726 / 10,674,553** |
| m2off2 (final, main-2 rule off) | 4.3583 | 4.2917 | 4.3817 | 2,484,473 / 1,598,389 / 10,703,969 |
| unp2 (final, both prunes open) | 4.3583 | 4.2917 | 4.3817 | 2,484,488 / 1,598,389 / 10,703,969 |

Per game (840 games):
- **base -> final: 0 faster, 0 slower** on every seed (digests differ in 7 / 7 / 36 games: same win turn,
  different line). There is no slower game to explain.
- new -> final: 8 faster, 0 slower (the 8 Legion Leadership games above, all recovered).
- m2off2 -> final and unp2 -> final: **1 faster, 0 slower** (s300 gi83 6 -> 5); s100 and s5000 0 / 0.
  The Nova main-2 rule saves 2.0% / 0.7% / 0.3% of units. The doomed-champion prune is inert again
  (unp2 == m2off2 in every game, units differ by 15 on s300).
- Census `MTG_CHAMPION_AXIS_STATS`: `searched-frame=0` on both arms (playout 4 / 2: beyond-horizon picks,
  permitted).
- **Units: the final binary spends +48% / +53% / +52% more search units than the Morph baseline at equal
  turns.** Morph's other hooks were outcome-neutral here but cut work: candidates are
  `TrickTargetsAttackersOnly` (Legion Leadership's target), `EndStepSacFaceUpDeadAfterCombat` (whose premise
  is FALSE for this deck, see above) and the attack/trick folds. None was taken over, since each encodes the
  Morph list's rules. A deck-specific, measured narrowing in their place is Stage 5f work (runtime-reducing
  heuristics); not done here.

**Byte-identity** (`build/scratch/redelem/b3/ident_b3.sh`, `D_base` vs `D_new2`, 8 games seed 4242 d3 b40
`--ignore-play-profile`): Landfall Elementals, Morph, Goblins, Spirits **32/32 logs identical** apart from
`runId`, stderr search counters identical (`build/scratch/redelem/D/id_base`, `id_new2`).

**Unit suite** (`build/Release/mtg-test`, full): **3082/3082 test cases pass** (3,596,253 assertions), rc 0.

## Soulstoke opponent end step (agents F / F2 / F3, 2026-11-14)

**What.** Incandescent Soulstoke's "{1}{R}, {T}: You may put an Elemental creature card from your hand onto the
battlefield. That creature gains haste until end of turn. Sacrifice it at the beginning of the next end step." can
now be activated at the end of the OPPONENT's turn as a searched choice. Until now it was main-phase only (a
PROVISIONAL partial in its `cards.json` note). The full design and code map are in
`docs/design/opponent-end-step-window.md` §0. In short:

- **Window.** Reuses morph's opponent-end-step window. `EotFaceUpMenu` (SpellEffects.h) crosses morph's flip menu
  with `EotPutOptions`: "put nothing", or a multiset of the distinct Elemental creature names in hand, at most one
  put per live source. A put Nova Chaser is crossed with its searched champion picks. One menu rank is the pin
  (`Plan::eot_face_up_choice` -> `GameState::scripted_eot_face_up`, the same field the flips use). The BoundaryFan
  fans it at every in-window crossing.
- **Holding the source.** Attacking taps Soulstoke, and the goldfish attack policy swings with everything. The
  HOLD is therefore searched in every host that decides a pre-combat main. In FSLineWin it is kind 4 of the
  searched dork attack/hold (`DorkAtkContestedKind`). In every other host it is a PLAN TWIN with
  `Plan::atk_dork_release = 4` (`AppendEotHoldTwins`, TurnSolver.cpp). Twins are gated on a usable source
  (`EotHoldTwinGate`). A twin whose hold no longer differs from the natural attack after the apply is skipped
  (`EotHoldContested`, a sound identity fold). A twin never takes a tie.
- **The put creature's own enter choices** (Flamekin Harbinger's tutor, Risen Reef's look-top land / order) are
  menu variants. That matters for Landfall Elementals only; no Red Elementals card has one.
- **Delayed sacrifice** stays the bool `Permanent::sac_at_end_step`. The window runs after the opponent's end-step
  sweeps, so the next sweep is OUR end step, which is "the next end step". An opponent-end-step delayed-sacrifice
  sweep (Soulstoke decks only) covers an opponent extra turn. Two unit tests prove the timing: a main-phase put
  dies at our end step that turn, and a window put survives our next turn's combat and dies at our end step.
  This is the coordinator's Q5 ruling ("prove the bool with tests, or switch to a turn stamp").
- **Viewer / claude-play:** a new `eot_put` decision (a modal card grid with "Put nothing"), after `face_up`.

**F3 fixes found while validating.**
1. *Twins were discarded as duplicates.* A twin's post-apply STATE equals its base's, because the hold is a
   combat pin. So every post-apply state dedup a twin met discarded it as its base's duplicate. Three places did
   this: the lookahead's axis-variant skip (`bp_seen_states` + `PlanDupSkippable`), `LaWaveDup` in the breakpoint
   wave, and the group wave. In all three the hold was unreachable, which breaks the completeness rule. The fix,
   `EotHoldFoldKey`, folds the kind-4 pin into those keys (and into the opt-in `MTG_CAND_DEDUP` key). It is
   the identity for every plan without kind 4.
2. *The look-top ORDER front.* Rank 0 of a top-tutor step under a Reef carried an order pin, so `menu[0]`
   differed from the `front_only` entry the playout takes. The unpinned default row now comes first. One of the
   k rows repeats it, as an inert duplicate (the default depends on the state at resolution). Caught by the unit
   test "a window put under a Risen Reef fans the look-top land / order pins".

**Validation (final binary = `build/scratch/redelem/F/new_bin`).**
- Unit suite (full `mtg-test`): **3095/3095 test cases pass** (3,596,816 assertions). Before the F3 fixes, 1 failed
  (the look-top front check, fix 2).
- `test/scenarios.sh`: **533/533 pass**, including the new
  `test/scenarios/redelem_soulstoke_opp_eot_firecat_leadership.json`. That fixture wins T4 only by holding
  Soulstoke and putting Blistering Firecat at the opponent's end step. The baseline binary fails it ("no win",
  opponent at 4).
- Paired A/B at d3 b100, `MTG_DUMP_WINS` per game. Base = `F_base` (HEAD `854902a73`, before agent F); new = the
  final binary. One pooled batch per arm (`build/scratch/redelem/F/man_ab.json`). Landfall Elementals runs with
  `ignore_play_profile` (its profile locks d6 value-play; d3 b100 keeps both decks on one setting).

| job | games | base avg | new avg | faster / slower | digest diffs | units base -> new |
|---|---|---|---|---|---|---|
| Red Elementals s300 | 120 | 4.3500 | 4.3500 | 0 / 0 | 0 | 2,435,831 -> 2,546,258 (**+4.5%**) |
| Red Elementals s100 | 120 | 4.2917 | 4.2917 | 0 / 0 | 0 | 1,587,726 -> 1,635,241 (**+3.0%**) |
| Red Elementals s5000 | 600 | 4.3817 | 4.3817 | 0 / 0 | 1 (gi104, same T5; a different T1 land) | 10,674,553 -> 11,086,020 (**+3.9%**) |
| Landfall Elementals s300 | 120 | 4.3667 | 4.3667 | 0 / 0 | 0 | 6,049,875 -> 6,035,405 (-0.2%) |
| Landfall Elementals s5000 | 600 | 4.3283 | 4.3283 | 0 / 0 | 2 (gi165, gi385, same turn) | 25,628,240 -> 25,530,926 (-0.4%) |

  **Net 0 turns on both decks, and no game is slower**, so there is nothing to repro at `--depth 8 --budget-ms
  0`. **No game is faster either: at d3 b100 the window put does not change a single win turn in 1,560 games.**
  The scenario shows the line is findable when it is the only kill. On these seeds the main-phase put or the
  attack is never worse. A traced re-run of both s300 jobs (240 games, `--game-trace-dir
  build/scratch/redelem/F3/tr`) found **no real window put**. The check: no Elemental appears on the battlefield
  between one turn's last phase and the next turn's draw. The only between-turn arrivals (4) were championed
  creatures (3 Soulstoke, 1 Metallic Mimic) returning when Nova Chaser was sacrificed. The game log records no event for a window put or a
  window flip; only the viewer's play-event sink does. So this count comes from the boards. Why the window
  rarely wins in a goldfish: a main-phase put with haste hits the same turn, so the window only gains when a
  body that arrives untapped next turn is worth more than this turn's hit. That happens with a lord or doubler
  (the fixture). (A preliminary run without fix 2, `build/scratch/redelem/F3/ab_b7`, gave the same
  digests and units for every job.)

- Census (`MTG_FACEUP_AXIS_STATS=1`, the new arm, all 1,560 games): `searched-frame=0` for flips/puts and for champion
  picks. `playout=24798`: beyond-horizon fronts, permitted. `outside-window=34`: crossings outside any search
  window, also permitted. `real-end-step=0`. `eot-put enter choices: fanned steps=15002 unfanned=0` (all from
  Landfall Elementals' Reef / Harbinger puts). `soulstoke hold: lookahead twins=603344 inert-skipped=158108
  fslinewin-shadowed-by-dork-kind=0`.
- **Byte-identity vs `F_base`** (`b3/ident_b3.sh`, 8 games, seed 4242, d3 b40 `--ignore-play-profile`):
  **Morph 8/8 logs identical and stderr identical** apart from `runId` and the log path. Goblins: the same. Spirits:
  logs identical, and stderr differs only in the wall-clock `SLOW-GAME` lines (same per-game units). Landfall
  Elementals: 8/8 logs identical. Its search counters moved, as expected for a deck with Soulstoke.
- Audits: `audit_card_costs.py` rc 0 (627/627 match Scryfall). `audit_card_fields.py` rc 0 (the three known
  oracle-text paraphrases: Hellspark, Nova Chaser, Thunderkin). `audit_viewer_decisions.py --no-sweep` rc 0 for
  both Red Elementals and Landfall Elementals; `eot_put` is expected for Soulstoke.

**6a disclosures (Soulstoke window).**
- ORDERING heuristic, `DecisionProvider::EotPutOrder` (base provider: printed power descending, then hand order). It
  orders the window put menu. It decides the front (the playout beyond the horizon, and ties) and the human's
  default. It prunes nothing. No deck provider overrides it.
- TIE rule: a hold twin never takes a tie, so on equal value the natural attack is played. This matches
  FSLineWin's strict `<` for kind 4.
- Sound folds (no line lost): the twin gate (no usable source before the apply means no twin), the inert-twin
  skip (after the apply the source would not attack, is tapped, or no matching card is left in hand, so the hold
  equals the natural attack), and the interchangeable-source multiset (every Soulstoke untaps at our untap
  step).
- Not carried, structurally: the this-turn kill checks (`LethalCutKillIsReal`, `WinlessSeedApplyWins`, the
  verify re-solve), because holding only removes damage. Also the diagnostic hosts (`EnumerateEarliestWins`, the
  fd-pred dump).
- Mana is held as in the morph precedent: whatever the turn leaves untapped is what the window can spend. There is
  no reservation axis, and the plan enumeration offers the lines that spend less.
- After a window put, the opponent's cleanup (CR 514.2) clears the put's until-end-of-turn haste. The put creature
  attacks on our turn because it has been ours since that turn began, not because of haste.

**Open questions (USER; none blocks work).**
- S1. FSLineWin runs ONE alternative combat per plan, so where a mana-dork kind (1-3) fires at the same node, the
  Soulstoke hold is not searched there. The census counts it (`fslinewin-shadowed-by-dork-kind`). Red Elementals
  has no dork, so it reads 0 here; Landfall Elementals' Smokebraider could trigger it.
- S2. Mana holding has no reservation axis (above). A multicolour payer that taps the wrong colour in main 2 could
  strand the {R}. That cannot happen in mono-red.
- S3. The put menu's front is the base `EotPutOrder` (largest power first). Should a Red Elementals order exist
  (e.g. Firecat before Ball Lightning)? It decides only ties and the playout.
- S4. Cost: the hold twins cost +4.5% / +3.0% / +3.9% search units on Red Elementals (s300 / s100 / s5000), and -0.2% / -0.4% on
  Landfall Elementals, at equal turns. The brief's threshold for hunting a collapse was +25%, so no
  narrowing was added.
- S5. Logging gap (not a play defect): the goldfish game log (`--log-dir` / `--game-trace-dir`) writes no
  action for a window put, just as it writes none for morph's window flip. The put creature simply appears on
  the next turn's board. Only the viewer's play-event sink shows the "Opponent's end step: ... puts ..." line.
  Should a logged `END_STEP_PUT` action be added? It would change log bytes for Morph too (its flips).
- S6. In 1,560 paired games at d3 b100 the window changes no win turn, and the 240 traced games contain no real
  window put. The window is a rules-completeness fix, and the units cost is S4. Keep it on by default? The
  default taken: on. It is a card-rules model, not a heuristic, and the coordinator's completeness ruling
  applies.

## Regression suite (4-bis/5j) (agent G, 2026-11-14)

Binary: `build/Release/mtg` at HEAD `0f16b98a2` (`HEAD:src` `b9f463c4`, rebuilt by `./build.sh`, a no-op).
Every tier used Release (`MTG_PGO=0` on the overnight run, so it did not wait on a PGO build). Results are
byte-identical across builds, so ground truth does not depend on the build.

**Cost gate.** `python3 scripts/suite_gate.py --cost "decks/Red Elementals"` ran one pooled regression run
(`--deck=redelem,hinata,hinata2hg`):
- redelem **3,562.9 ms/game**, with the worst searched shape at `redelem_regression_d5_b20`.
- hinata **21,704.9 ms/game**.
- Ratio **0.16x** against the 3x limit: **PASS**.
- redelem's regression tier costs 1,225 core-s over 6 jobs.
- In search units, redelem uses ~18.2-18.9k units per d5 b20 game, against Hinata's 62-79k (~0.27x).
- The box was 12 threads and fully loaded, so the ms are thread-wall.
- Hinata's 13 regression keys in that run were byte-identical to GT.

**Rows** (`test/regression_cases.sh`, key `redelem`). They use martyr's shape, which is also Hinata's:
- **Play rows.** There is no `value_play` block, so the play setting is d5 b20, and the play rows are d5 b20.
  Each tier also has one d3 b10 sanity row.
- **SMOKE:**
  - `d0 1001 x1000`
  - `d3 1001 x50 b10`
  - `d5 1001 x100 b20`
  - `redelem2hg d5 1001 x25 b20`
- **REGRESSION:**
  - `d0 2002 x1000`
  - `d3 2002 x50 b10`
  - `d5 2002 x150 b20`
  - `d5 3003 x150 b20`
- **OVERNIGHT (the pre-keep-table ENTRY set):**
  - `d0 4004/6006/8008/10010 x2000`
  - `d3 4004 x125 b10`
  - `d5 4004/5005/6006/7007 x300 b20`
  - It grows to the FULL set just before the keep-table adoption (regression-testing skill, "Sizing a
    deck's overnight rows").
- **2HG: one smoke canary only.** No card reads heads or opponents:
  - no "each opponent" effect;
  - no `attack_tokens_per_opponent`;
  - no life-total gate;
  - Sunspine Lynx's ETB hits "each player" per NONBASIC land, which is inert against the land-less goldfish;
  - Lightning Bolt is "any target".

  Only the 30-life clock moves, so the deck gets the standard 25-game searched canary, as `elementals2hg`
  does.
- **Unbudgeted backstop.**
  - The screen: 16 five-game blocks, `30001..30301` step 20, at d3 and d5, budget 0, `abandon_units`
    2,000,000, `MTG_SLOW_GAME_MS=5000`. Scratch is in `build/scratch/redelem/G/bs/`.
  - **No game was abandoned in any block, but the tail is heavy:**
    - `s30121` d5, gi2: 0.97M units / 60 s. The same game at d3: 0.29M units / 26 s.
    - `s30041` gi3: 18-22 s at both depths.
    - `s30301` d5 gi3: 21 s.
  - **About 130 us of wall per unit, unbudgeted.** That is roughly 3x martyr's ratio, which points to work
    the unit meter does not count. It is worth a look in Stage 5f.
  - **Blocks kept:** `30201` (REGRESSION) and `30241` (OVERNIGHT). In both, no game took over 5 s at
    either depth, and each block used ~11-29k units. They are 5-game blocks, the heavy-deck size.
- **Tier shares.** Smoke share (`test/tier_cost.py smoke`): redelem 0.8% and redelem2hg 0.4%. The
  redelem-only overnight run's makespan was 5m19s (11 jobs).

**GT accepted.** Each tier was run, inspected, then accepted with the same `--deck=` filter. Before each
accept, `regression_gt.txt` and `gt_logs/` were backed up, and the diff afterwards showed only the new
`redelem*` keys plus the header line.

| key | GT (avg win turn / digest) |
|---|---|
| redelem_smoke_d0_s1001 | 4.8780/d04cc672b6a6e173 |
| redelem_smoke_d3_s1001 | 4.4000/07d5ee5aefad646a |
| redelem_smoke_d5_s1001 | 4.3300/e9296f6c95548caf |
| redelem2hg_smoke_d5_s1001 | 5.0400/88328de670a060fb |
| redelem_regression_d0_s2002 | 4.8800/bafe02dc046e9500 |
| redelem_regression_d3_s2002 | 4.4800/7a6b1d131cba7893 |
| redelem_regression_d5_s2002 | 4.4000/c014564161dea3bc |
| redelem_regression_d5_s3003 | 4.3733/82d7ce52f943d32b |
| redelem_regression_d3/d5_s30201 (backstop) | 4.0000/787bde01e16f33b1 (both) |
| redelem_overnight_d0_s4004/6006/8008/10010 | 4.8635 / 4.8890 / 4.8760 / 4.8865 |
| redelem_overnight_d3_s4004 | 4.3680/58fff9404f3bf554 |
| redelem_overnight_d5_s4004/5005/6006/7007 | 4.3700 / 4.3833 / 4.3567 / 4.3733 |
| redelem_overnight_d3/d5_s30241 (backstop) | 4.2000/bea2b901952c81ef (both) |

The d3 and d5 backstop blocks give the same digest, in every block of the screen too. This is normal for
unbudgeted play: 61 of the suite's 94 backstop d3/d5 pairs coincide in GT. Unbudgeted d3 also spends ~2x the
units of d5 on this deck.

**Landfall Elementals (`elementals`), Soulstoke window attribution.**
- **Smoke** (`elementals` + `elementals2hg`, inside the full smoke run): **byte-identical**.
- **Regression.** 4 keys are byte-identical. 2 keys moved play at the same score:
  `elementals_regression_d3_s2002` and `..._d5_s2002`. Both are the same game, gi22 (seed 2024).
  - **Attribution.** `F_base` (`854902a73`, pre-Soulstoke) and `D_base` (`44154dd93`) both reproduce the GT
    per-game digests. Only `06f2e9c98` (the Soulstoke window) touches `src` since `F_base`.
  - **Old line.** T4: cast the second Flamekin Harbinger for {R} in main 1, attack with Soulstoke (opponent
    to 11). T5: win.
  - **New line.** T4: hold Harbinger, keep Soulstoke untapped with {1}{R} open, attack without it
    (opponent to 13). At the opponent's end step, the **window puts Harbinger**. It is on T5's board
    without a cast; the put itself is not logged (S5). Harbinger's ETB tutor still sets up Brushfire, and
    T5 wins.
  - **Verdict.** It is a clean like-for-like line change: kept hand and draws are identical, the win turn
    is unchanged at both depths, and the loss-penalized net is 0. Accepted with
    `--regression --accept --deck=elementals`. The diff showed exactly those 2 keys and their 2 `.wins`
    files.
  - **This is a REAL window put in shipped LFE play.** That updates S6, which found none in the 240 traced
    d3 b100 games.
- **Overnight.** LFE overnight GT was not run. It waits for the periodic overnight run.

**Identity.**
- The FULL smoke tier: **203 passed, 0 failed**, plus 4 new redelem keys. Every other deck is
  byte-identical to GT, including Morph, Goblins, Spirits, LFE and every 2HG canary. Makespan was 106m31s
  on the 12-thread box. The first ~6 min overlapped the LFE regression run.
- Hinata and hinata2hg regression: byte-identical, from the cost-gate run.
- `python3 test/check_gt_logs.py`: consistent 1191, STALE 0, missing 0.
- `python3 scripts/verify_deck.py "decks/Red Elementals/Red Elementals.cod" --no-sweep`: **GATE PASS**.
  - **`regression_tiers` PASS** ("in all 3 suite tiers with GT (redelem)").
  - Also PASS: coverage, card_costs, card_fields, viewer (static), viewer_wiring and suite.
  - The runtime gates (mismatch, play_invariants, viewer sweep) and claude_sweep are SKIPPED; they belong to
    Stage 5.

## Stage 5 — verification

### Stage 5 runtime verification (agents H/H2, 2026-11-14)

Scratch (regenerable, build volume): `build/scratch/redelem/H/` and `build/scratch/redelem/H2/`. Binaries:
HEAD `cfa0d2c13` src (snapshot `H/bin_cfa0/mtg`) unless stated. Per-game parsing: `H/wins.py <err> [jobA jobB]`
(reads `MTG_DUMP_WINS` `[win]` lines; prints per-job avg and the paired faster/slower list).

**5a mismatch harnesses: PASS (0 lines).** One pooled batch (`H/man_5ab.json`, `MTG_FLAG_NONCONV=1
MTG_FD_ORACLE=1 MTG_DUMP_WINS=1`): s7100 x200 at d0, d3 b10, d3 b20, d5 b20, d5 b100; s7400 x200 at d3 b10 and
d5 b20; s1001 x100 at d5 b20. 1,500 games, **0 `[nonconv]`, 0 `[fd-diverge]`**. Flag inertness: the s1001 d5 b20
job reproduces smoke GT exactly (4.3300, digest `e9296f6c95548caf`). (`MTG_FULL_DEPTH` is retired; full depth
is the default.)

**5b depth sweep: PASS (monotone).**

| seed x200 | d0 | d3 b10 | d3 b20 | d5 b20 (play) | d5 b100 |
|---|---|---|---|---|---|
| s7100 | 4.9700 | 4.3750 | 4.3750 | 4.3750 (`ec8b883b0636ad67`) | 4.3700 |
| s7400 | -- | 4.4050 | -- | 4.4050 | -- |

Per game: d0 -> d3 103 faster / 0 slower; d3 -> d5 0 / 0 on both seeds; d5 b20 -> b100 1 faster (s7100 gi83,
5 -> 4), 0 slower. The clock matches the deck's ~T4.3-4.4 shape. Slow-turn outliers, all read at d5 b20 (logs
`H/out_<base>_<gi>/`, no mismatch flags): s7100 gi1 and gi10 (wt 6, two lands until T5/T6); s7400 gi8 (wt 7,
two Mountains all game, Firecat / Soulstoke x2 uncastable), gi80 (wt 7, mulligan to 5, one land), gi45 / 74 /
126 (wt 6, mulligan or short on lands); s1001 gi46 (wt 7, a smoke GT game: 8 lands in 12 cards, no Elemental left
by T5; traced by hand, the best alternative also wins T7). All mana screw or flood; none is a search defect.

**5c budget starvation (agent H2): the play budget is adequate.** One pooled batch (`H2/man_5c.json`,
fold binary `38454a1a0`): the nine slowest 5b games, each at d5 b20 and d5 b2000 (100x).

| game | b20 | b2000 | |
|---|---|---|---|
| s7100 gi1 / gi10 | 6 / 6 | 6 / 6 | screw, not budget |
| s7100 gi83 | 5 | **4** | STARVED at b20 (already 4 at b100, 5b) |
| s7400 gi8 / gi80 | 7 / 7 | 7 / 7 | screw / mulligan to 5 |
| s7400 gi45 / gi74 / gi126 | 6 / 6 / 6 | 6 / 6 / 6 | mulligan / short on lands |
| s1001 gi46 (smoke GT) | 6 | 6 | STARVED at b20 with the fold OFF (7 at b20, 6 at b2000); the fold frees enough budget to find the T6 win at b20 |

So two of nine games are budget-limited at play settings. Both recover monotonically with budget, so neither is a
logic bug; 7 of 9 are identical at 100x budget. That corrects agent H's hand trace of s1001 gi46 ("no faster line
exists"): a T6 line exists, and the search finds it at b2000 OFF or b20 ON. Input to play-budget sizing only;
d5 b20 stays the play setting (the deck's profile, USER-owned settings are unchanged).

**5c2 horizon-honest tie-break (`leaf_tiebreak_check.py`): NO SIGN at 12 and 48 blocks; default remains ON.**
* Run 1 (agent H): 12 blocks x 1,000 games x 2 arms = 24,000 games at play settings (d5 b20), seeds
  1,000,000-2,100,000, HEAD `cfa0d2c13` snapshot, `MTG_PGO=0`. **0 changed games of 12,000 paired (0.000%)**,
  net +0 on both halves. Verdict NO SIGN AT THIS SAMPLE; the script asks for `--blocks 48`.
* Run 2 (agent H2, launched 2026-11-14 21:40 UTC): `--blocks 48 --seed-base 3000000` (96,000 games, fresh seeds,
  no overlap with run 1) on the fold binary `38454a1a0` (snapshot `build/scratch/redelem/H2/bin_fold/mtg`,
  `MTG_BIN` override, `MTG_PGO=0`). Out `logs/leaf_tiebreak/redelem_H2_b48.out`, verdict
  `build/scratch/redelem/H2/ltb48.log`, marker `H2/ltb48.DONE` (`rc=0`). Of 48,000 paired games,
  **one changed, one worse, zero better**; each 24,000-pair half had at most one change. The script reports
  NO SIGN (below its 20-change threshold) and estimates 481 blocks for a directional sample. Keep the
  default ON under the predeclared no-sign rule; this does not establish a benefit for this deck.
* Reading: a deck that wins at ~T4.4 well inside the d5 horizon rarely reaches a no-win horizon, so the tie-break
  almost never binds. That is consistent with 0 / 12,000.

**`verify_deck.py` (agent H2, after the fold; second run after two tooling fixes).**
* PASS: coverage, card_costs, card_fields, regression_tiers, viewer_wiring, mismatch (seeds 7001/8001 x 60),
  suite.
* PASS: play_invariants, 8 games / 140 decisions. This needed `gy_return_one` / `face_up` / `eot_put` added to
  `scripts/play_invariants.py` KNOWN_TYPES; run 1 failed on "unknown decision type gy_return_one".
* SKIP: claude_sweep (PENDING until 5d runs) and clause_ledger.
* **FAIL viewer: Incandescent Soulstoke `eot_put` HARD MISS.** The emitter is wired. The window needs the source
  UNTAPPED at the opponent's end step, but the engine decides combat in the claude-play protocol (no attack/hold
  axis in the plans), so Soulstoke attacks every turn and the window never opens.
  * Agent H2 added the `pick_eot_put_window` forcing and a NOT_FORCED classification to `verify_card`
    (`scripts/audit_viewer_decisions.py`). The SWEEP path still reports HARD MISS: not yet traced to where the
    sweep decides it.
  * Underlying USER question: a human cannot hold an attacker back in the viewer, so the human can practically
    never use the window.
  * Next agent: either route the sweep's verdict through `verify_card`'s NOT_FORCED, or record a USER-approved
    deferral in "## Approved deferrals" (never self-approved).

**5f runtime.** Full record: `docs/design/red-elementals-search-cost.md`.
* (b) The "130 us/unit at budget 0, 3x martyr" anomaly (open question 12) is EXPLAINED and is not a defect: it
  is the lookahead bottoming of mulligans (no keep table, so each bottom plays clairvoyant full games,
  unbudgeted at budget 0). s30123 gi2 d5 b0: 141.0 s, of which 140.99 s is bottoming; all 29 SLOW-GAME lines
  in 5a are mulligan games; budgeted cost per unit is 254 us vs martyr 344 (d5 b20). Remedy: the keep table.
* (a) The +50% units: Morph's `EndStepSacFaceUpDeadAfterCombat` is NOT sound here (a post-combat Spark /
  Hellspark / Ball Lightning feeds Thunderkin, unearth and a post-combat champion) and is not taken. Morph's
  largest-attacker trick target is lossy (USER proposal, not adopted).

* (a) ADOPTED (sound dominance fold, default ON): `MTG_REDELEM_TRICK_FOLD` (heurarm `REDELEM_TRICK_FOLD`),
  `RedElementalsProvider::TrickTargetsAttackersOnly` + `TrickTargetCandidates`. Legion Leadership is not aimed
  at a creature that cannot attack this turn and is not cast once combat is over. The proof was re-checked card
  by card against `cards.json` for all 14 names (agent H2): no cast trigger, no spells-cast or graveyard/instant
  count, nothing reads a non-attacker's power (Thunderkin reads toughness), champion / unearth / Soulstoke
  unaffected, no mid-combat cast window (the doubler-after-exalted window needs exalted, which the list lacks).
  Table in `red-elementals-search-cost.md`. **A/B, one pooled batch, 1,240 paired games (d3 b100 s300 x120,
  s100 x120, s5000 x600; d5 b20 s7100 x200, s7400 x200): 0 faster, 0 slower, net 0; units -4.9 / -4.7 / -4.4 /
  -2.4 / -3.6% (-4.0% total).** The OFF arm reproduces HEAD (s7100 d5 b20 4.3750 `ec8b883b0636ad67`; s7400
  `c3c1c02f1ca1ce42`). Unit suite 3,096 / 3,096. Off: `MTG_REDELEM_TRICK_FOLD=0`; human play and
  `MTG_UNPRUNE=tricktarget` keep every target.
* GT re-accepted for `redelem` only (`d7d0d55b6`; commit `38454a1a0` binary, attributed per game). Smoke: s1001
  gi46 7 -> 6 at d3 and d5; d0 gi143 7 -> 6, gi391 8 -> 7, gi861 6 -> 7. gi861 is the greedy d0 runner keeping
  Leadership in hand and then skipping a T5 Fiery Islet draw; at d5 b20 and at d8 b0 it wins T5. `redelem2hg`
  is byte-identical. Regression: 0 slower / 0 faster; 4 searched same-score play changes, each the dead Leadership
  cast removed; d0 has 22 play changes and 0 score moves. No other deck routes to `RedElementalsProvider`.
* 6a DISCLOSURE (modelling gap, pre-existing, both arms): Legion Leadership cannot be cast DURING combat, so it
  cannot double a creature Thunderkin Awakener returned tapped and attacking (a real line: a returned Ball
  Lightning 6 -> 12). USER question: generalise the doubler-after-exalted window to after attack triggers?
* USER proposal (lossy, NOT adopted): Morph's largest-attacker trick target (one cast-time Leadership target).

## Approved deferrals
(none yet)

## Open questions for the user
- PROVISIONAL (Nova Chaser, champion): every in-window pick is now SEARCHED (`b53c6f21c`; shapes in "Agent
  B2/B3 notes"). The census `MTG_CHAMPION_AXIS_STATS` read 0 unpinned picks in every bucket over 1,800
  game runs (840 games x 2 prune arms + the repro).
  Two gaps remain. (1) A same-line Elemental TOKEN maker is not offered as an entrant by
  `ChampionSameLineEntrants`. No such card is in any list. (2) A CAST Nova championing a Nova carries no
  return rank: in a goldfish a cast Nova never leaves, and if one did, its returning Nova's pick would be the
  unpinned front and the census would count it. Only a PUT Nova (Soulstoke, sacrificed at end step) has
  return ranks. Sign-off owed.
- 6a DISCLOSURE, provider ORDERING heuristic (Nova Chaser): `DecisionProvider::OrderChampionCandidates`
  (USER: *"I think we could have a heuristic that orders targets for champion most likely."*). Order: doomed
  first (sac_at_end_step / unearthed / exile_at_end / endstep_sacrifice_self), then attacked or tapped,
  then smallest power, then lowest card number. It only ORDERS: it sets the emission order of the cast and
  put variants (same-line entrants follow, the decline is last), the rank order of a returning champion's
  pick, the beyond-horizon playout pick, and the human's highlighted default. Lives in `RedElementalsProvider` since
  Stage 4a (the root hook is neutral).
- 6a DISCLOSURE + USER DECISION, provider PRUNING heuristic (Nova Chaser):
  `DecisionProvider::PruneChampionCandidates`, `MTG_CHAMPION_DOOMED_PRUNE`, **default ON** (USER: *"No, the
  provider can prune other options."*). When a candidate is doomed AND spent (it attacked or is tapped), it is
  admitted alone, with no decline and no same-line entrants. Disclosed cost: the doomed card goes to exile
  rather than the graveyard, so Thunderkin / unearth cannot reach it while Nova lives. The full set is
  available via `MTG_UNPRUNE=champion` and in human play. **A/B at d3 b100, default vs unpruned: seeds 300
  x120, 100 x120, 5000 x600 (840 games): avgs 4.6500 / 4.6333 / 4.7267 on BOTH arms, 840/840 per-game
  digests identical (0 faster, 0 slower), search units identical except s300 (1,630,112 vs 1,630,115: the
  prune saves 3).** So it is inert at this setting, because the shape is rare. The default is unchanged
  pending the user's decision. It lives in `RedElementalsProvider` since Stage 4a (root neutral).
- PROVISIONAL (Nova Chaser): `DeckUsesSecondMain` now returns true for any champion deck, opening a full
  searched post-combat main on every turn for Red Elementals (needed for the doomed-attacker line). Its
  cost is unmeasured; a restricted main-2 (admit only champion casts/puts, the OMNITOOL_M2 shape) is the
  alternative if it is expensive. The user's call.
- PROVISIONAL (Nova Chaser): not modelled (unreachable in a goldfish) -- the champion leaving before its
  enter trigger resolves; a same-plan Elemental TOKEN maker is not offered by
  `ChampionSameLineEntrants` (none exists in any list).
- PROVISIONAL (Hellspark Elemental, unearth): the "exile it if it would leave" replacement is routed
  only through the shared sacrifice chokepoint (and the end-step exile sweep); destroy effects, the
  lethal-damage / toughness-0 SBA, the legend rule, creature sweeps and bounce are NOT routed. None can
  befall an unearthed creature in this list (passive opponent, nothing of ours damages/shrinks/bounces
  our creatures). Sign-off owed.
- PROVISIONAL (Sunspine Lynx): both statics are modelled at every site the engine models lifegain /
  prevention, but are inert here (no lifegain or prevention in the list or from the opponent).
- Viewer audit INERT_PARAMS additions (need the user's OK): `etb_damage_each_player_per_nonbasic_land`,
  `static_players_cant_gain_life`, `static_damage_cant_be_prevented` (no player choice).
- Viewer audit INERT_PARAMS additions (Thunderkin Awakener, `05b603a2e`, need the user's OK):
  `attack_trigger_reanimate_subtype`, `attack_trigger_reanimate_toughness_lt_self` (target filters of the
  `gy_return_one` pick), `attack_trigger_reanimate_sac_end_step` (mandatory delayed sacrifice).
- PROVISIONAL (Thunderkin Awakener): two triggers never target the same card (the second would only
  fizzle) -- disclosed as dominated.
- PROVISIONAL (Thunderkin Awakener): a RETURNED Nova championing a Nova -- the exiled Nova's own return
  pick is unpinned (ret=0). An edge case; the census read 0 unpinned picks over the 120-game repro.
- PROVISIONAL (Thunderkin Awakener): axis completeness rests on a toughness upper bound plus headroom
  (champion cards in hand, same-line entrant names); ranks past the real list clamp to its last entry, and
  the duplicates are folded after combat by `MTG_COMBAT_PIN_FOLD`.
- PROVISIONAL (Thunderkin Awakener): the pin holds 7 trigger ordinals x 16 ranks; past that the front is
  taken (unreachable in this list: 4 copies, at most 4 attack triggers).
- 6a DISCLOSURE, provider ORDERING heuristic (Thunderkin Awakener): `DecisionProvider::AttackReanimateTargets`
  orders the target ranks (printed power descending, then a card without a graveyard ability before an
  unearthable one). It sets rank order, the beyond-horizon playout pick and the human's default; it prunes
  nothing. Lives in `RedElementalsProvider` since Stage 4a (root neutral).
- 6a DISCLOSURE + USER DECISION, provider RESTRICTION (Nova Chaser main 2, Stage 4a):
  `RedElementalsProvider::CollectedActionAllowed`, `MTG_REDELEM_NOVA_MAIN2`, **default ON**, implementing the
  USER's *"Note that Nova Chaser should be cast main 2 if it is hardcast, but it might also be dropped in main 1
  via Incandescent Soulstoke."* A champion creature with no haste access is not hardcast pre-combat; the
  Soulstoke put stays in both mains. Line given up: a main-1 Nova that declines champion, then is returned
  attacking by a Thunderkin pumped to toughness >= 3. A/B (840 games): 1 faster (s300 gi83 6 -> 5), 0 slower vs
  the rule off and vs `MTG_UNPRUNE=champion,championmain2`; saves 2.0% / 0.7% / 0.3% units. Confirm it as built.
- 6a DISCLOSURE + USER DECISION (CAST ORDER, USER-OWNED, PENDING): `RedElementalsProvider::OrderOpaqueCastsByRank`
  = true, `MTG_REDELEM_OPAQUE_ORDER`, **default ON**, because it restores the order the deck had while it rode
  MorphProvider. It rank-sorts the order-opaque cast set (Legion Leadership and the casts around it) by the
  generic `CastOrderRank`, so the doubler follows the bodies it doubles. Without it, 8 games were slower than
  the Morph baseline (all Legion Leadership lines); with it, 0 slower and 0 faster vs Morph in 840 games. The
  user may want a deck cast order instead (Stage 5e); until they answer, this stands as the restored default,
  not as an adopted order.
- Stage 4a COST, for the user: leaving MorphProvider costs **+48% / +53% / +52% search units** (s300 / s100 /
  s5000) at equal turns. Morph's work-cutting hooks (e.g. `TrickTargetsAttackersOnly`,
  `EndStepSacFaceUpDeadAfterCombat`, whose premise is false for this deck) were not taken over. A measured
  deck-specific narrowing belongs to Stage 5f.

## User reference shortfall and resolution (2026-11-15)

Ten user-played references are committed in `references/Red_Elementals/` (`ae3eca75f`). Before the fix,
the shipped d5 b20 search averaged 4.400 turns against the user's 4.400: eight ties, one search faster, one
search slower. In `claude_s1_gi0.json` the user wins T4, search T5 even at d8 b0 and with the
Red Elementals provider prunes disabled. The user's T4 sequence is Soulstoke put Ball Lightning,
then cast Legion Leadership targeting that Ball Lightning. `CollectActions` emits Leadership
targets from the pre-plan battlefield and from same-plan *hand casts*, but not from same-plan
Soulstoke puts (`TurnSolver.cpp` solo-target-trick emission). The autonomous apply executes all
casts before trailing `PutFromHandAbility` activations. Thus no searched plan can express the
user's put-then-trick order. Human play's explicit sequence can express it and the target prompt
sees Ball Lightning after the put. This was a search-completeness defect, not budget starvation.

The solver now emits a same-plan Soulstoke put target for Leadership, requires that put in the
subset, and executes it before the trick in both rollout and real execution. The variant is
available when Soulstoke grants the creature haste, including Nova Chaser. The rebased Release
binary wins `s1 gi0` on T4 at d8 b0, and its trace puts Ball Lightning before Leadership.
`ref_bench.py --deck red_elementals` passes all ten references: human 4.400, search 4.300,
zero shortfalls. The human viewer can also choose the put as one main-phase action and cast
Leadership on the entered creature in its next main-phase decision (verified with seed 730013).

Mulligan feasibility has started: `MTG_EQUIV_DISCOVER=1`, 400 probes, threshold 0.01 at depth 5
completed on the 12-core box. It found **13 equivalence classes** among 14 distinct cards; only
Mountain and Fiery Islet merged. The size-7 composition space is `C(19,7) = 50,388` hands.
This is a feasibility count, not a generation-time projection. Run the `recommend` scout on the
corrected, frozen play engine before choosing `fast` or `complete`.

The rebased Stage 5h dynamic Red viewer audit (8 games, seed 620000) exits 0 with all expected
decisions surfaced. Its targeted forcing check holds Soulstoke untapped and verifies `eot_put`
on seed 620000; Blistering Firecat's `face_up` also verifies. The earlier Landfall sweep
(6 games, seed 620100) exited 0; it predates the rebase and should be repeated at the final gate.

## Codex-play sweep

- commit: `038d905a6` (play source; subsequent baseline and documentation commits do not change play)
- seeds: 730000–730014; games: 15; choices: `logs/redelem/codex-play-730000-730014.json`
- result: 12 equal win turns, three Codex wins one turn later, zero Codex wins earlier than the
  autonomous d5 b20 search. The protocol exercised mulligan and bottom choices, Lightning Bolt
  and Leadership targets, Mimic's subtype, Nova champion, and Thunderkin graveyard returns.
- flags: 0 unresolved
- The three Codex subagents initially had no shell/filesystem tools. The parent Codex session
  drove all 15 distinct seeds directly using the same external-play protocol and checked the
  state transitions against the deck's `cards.json` definitions. This is a Codex-play sweep;
  no games are attributed to the tool-less subagents.

The integrated gate passes on the rebased source: coverage 14/14, card costs against Scryfall,
card fields, all regression tiers, viewer surface and GUI wiring, mismatch (seeds 7001/8001 x
60, both arms), play invariants (8 games, 148 decisions), sweep and suite. The first full run
found the `play_invariants.py` harness missing the upstream `choose_creature_type` decision;
it now follows Mimic's emitted Elemental default, and the repeated runtime gate passes.
`test/check_gt_logs.py` reports 1,226 consistent logs, zero stale or missing. The Red searched
regression changes after the put-before-trick fix are only improvements: smoke 7 faster,
regular 13 faster, overnight 51 faster, no slower games at any tier. The old baseline binary
cannot replay against the rebased database (it does not know the new `Islandwalk` keyword),
so the same-score digest changes cannot be line-diffed with `explain_game.py`; their scores
and strict reference outcomes are unchanged.

Human combat still uses the automatic attacker set. A player cannot deliberately hold
Soulstoke to open its opponent-end-step put window; the targeted viewer audit reaches that
prompt by forcing a legal hold. The autonomous search does evaluate hold/attack alternatives.
This is a disclosed viewer interaction gap, not an unmodeled card effect.

## Stage 5i discard bucket proposal (not adopted)

The Stage 4 analyzer reported `STATUS_QUO_OK`: two cleanup discard decisions in 400 d3 games.
The proposed quota-first keep set, net of permanents already on the battlefield, is:

| bucket | hand quota and preference |
|---|---|
| Mana | Keep the next two land drops early, then one; count Mountain, Fiery Islet and Legion Stronghold as lands, but use the Leadership spell face when it makes a lethal attack. Keep red coverage before excess utility lands. |
| Threats | Keep at least two available attackers or damage spells, counting a ready Thunderkin in the recursion bucket toward this floor. Prioritize a current-turn lethal Ball Lightning, Firecat or Spark, then other hasty bodies and Lightning Bolt. |
| Enablers | Keep one Soulstoke when a puttable Elemental is in hand, one Leadership when a viable attacker is present, and one Metallic Mimic if it can enter before a useful Elemental. Extra copies are overflow. |
| Recursion | Keep one Thunderkin when a returnable Elemental is or will soon be in the graveyard; Hellspark with unearth can count as its own threat. |

Within overflow, shed the card furthest from being playable with current board and mana; break
ties by duplicate redundancy. This is a proposal for the user's review. It has not been coded or
validated against searched labels, and the generic cleanup ranking remains live.

## Integrated verification after upstream Boars/Merfolk/Shadowborn (2026-11-15)

The Red branch was rebased onto upstream `f92bb3b7c`. The current Release engine retains the
Soulstoke put-then-Leadership T4 reference line; all ten reference games pass at human 4.400 and
search 4.300 turns, zero shortfalls. Red smoke (3 rows), regular (6) and overnight (11) all match
the accepted scores and play digests exactly, with no changed games. The focused Red unit binary
passed 76 tests and 1,623 assertions. The regular tier's strict viewer reference protocol passed
864 references, and the scenario suite passed 561 scenarios.

The integrated `verify_deck.py --no-network` gate passes: 14/14 full card coverage, field audit,
viewer surface and GUI wiring, mismatch seeds 7001/8001 × 60 in both arms, 8 invariant games / 148
decisions, recorded Codex-play sweep, and suite registration. The new upstream `creature_type`
prompt was added to the invariant driver; its focused rerun passed 4 games / 76 decisions. The
earlier network cost audit passed before this upstream rebase. Two Landfall Elementals regular
summary digests were repaired after a filtered rerun showed all six committed per-game logs
unchanged. Global ground-truth logs are 1,251 consistent, zero stale or missing. Reference safety
passes. The exhaustive mulligan profile is the next stage on this frozen play source.

## Exhaustive mulligan profile (2026-11-15)

The generation-setting screen selected d1/b3, with 13 expected buckets, rank fidelity 1.0000,
rho 0.9808 and 0.18× the d5/b20 play cost. The optimized `recommend` scout confirmed that only
Mountain and Fiery Islet merge. It counted **39,945** distinct seven-card hands after deck-copy
constraints (the earlier 50,388 `C(19,7)` figure was an unconstrained upper bound), and 126,958
play/draw cells across all hand sizes. Its floor took 727 seconds and projected `fast` at ~4 hours.

The PGO+LTO `fast` R30 generation reused the scout floor and completed in 2 hours 17 minutes.
The structural gate confirmed 39,945 size-seven entries, 47,068 smaller-hand cells, bottoming
enabled, and at least R2 for every cell. The first-version in-game A/B passed both gates:

| policy comparison | 1,000-game seed pairs | arm A | exhaustive arm | delta, exhaustive minus A |
|---|---:|---:|---:|---:|
| keep vs static, same bottoming | 4/4 favorable | 4.3403 | 4.2385 | -0.101750 turns |
| bottoming vs lookahead, confound mode 3 | 4/4 favorable | 4.3375 | 4.3083 | -0.029250 turns |

Both stopped early under the driver's predeclared significance rule (t=-15.3 and t=-8.09).
The regular-tier visibility run moved six Red rows: searched games 44 faster, 28 slower; d0
194 faster, 97 slower. The strict viewer protocol passed 864 references. The adoption audit
replayed all 1,360 regular-tier games with the table off and on, reproducing both baselines:
549 changed mulligan count, 130 changed bottom at the same count, two card-order-only digests,
and **zero unexplained**. The profile is accepted by the A/B gates. After the first final-engine
rebase onto `69c69f2ae`, all six Red regular scores and digests matched this table-on run
byte for byte. All ten references passed at human 4.400 / search 4.300 with zero shortfalls;
the integrated verifier passed 14/14 card coverage, viewer, mismatch, invariants and suite
registration. Strict viewer replay passed 864 references, all 561 scenarios passed, and the
reference safety scan passed. Smoke and regular GT were accepted after per-game attribution;
`check_gt_logs.py` found 1,252 consistent logs and none stale or missing.

The keep-table adoption raised Red's four overnight d5 play rows from 300 to 425 games
per seed at shipped b20, matching Hinata's play-row game floor. On the freshly trained
PGO+LTO engine, the resized table-off baseline was accepted first: the original 300-game
prefix had no play changes, and the four new averages were 4.3506, 4.3718, 4.3271 and
4.3388. The table-on averages were 4.2918, 4.3059, 4.2894 and 4.3435, respectively:
4.3471 → 4.3077 turns across 1,700 paired searched games (239 faster, 173 slower).
D0 improved on all four seeds. The overnight adoption audit reproduced both sides and
classified 4,001 changed mulligan counts, 925 changed bottoms at the same count, 29
bottom-order-only digests and **zero unexplained** changes. The table-on overnight GT was
accepted, and `check_gt_logs.py` found 1,252 consistent logs, zero stale or missing.
The accepted GT was rebased onto shared upstream `6cb9a7c85` (Gods decision and dredge
changes). The pooled final-source Red smoke, regular and overnight run passed every row,
including the 2HG canary, with identical scores and digests. Strict viewer replay again
passed 864 references, with zero play drift or enum gaps. The shared branch was pushed at
`a27d7bf8a`. The primary checkout is on the shared branch, and its viewer serves Red
Elementals on port 8080 using the verified Release binary and persistent reference backup.
The superseded local `viewer/redelem` branch was removed after the reference safety scan.

## Value leaf and play setting (2026-11-17)

`scripts/valueleaf.sh run` on frozen `24949783a` (src tree `aee55374648c`, PGO+LTO), started by Codex
and finished by Claude. Phase A dumped 10,774 rows from 2,500 games; the 120-tree model has a held-out
RMSE of 0.3358 turns. The C matrix (52 cells) was clean and phase D nominated trust depth 5 for the
staged model.

Phase E, 8 held-out seeds x 1,000 paired games against live (no sidecar, d5/b20, 4.29288):

| arm | avg turn | delta | paired t | work units |
|---|---:|---:|---:|---:|
| staged learned leaf | 4.29250 | -0.00038 | -0.50 | 0.773x |
| no-leaf escalation (`nlesc`) | 4.29175 | -0.00113 | -1.62 | 0.749x |
| **no-leaf final-depth single pass (`nlfit`)** | **4.29188** | **-0.00100** | **-1.37** | **0.699x** |
| emulation | 4.29350 | +0.00062 | +2.38 | 0.895x |

E.5 adopted `nlfit`, the cheapest passing arm. The learned model stays staged at
`logs/eval/Red Elementals.value.STAGED.json` and is not shipped. E.7 gridded d5/d6 x b20/b30/b40
for all four shapes on fresh seeds (7505000..) and adopted **d6/b20**, the cheapest passing arm (0.993x,
delta 0.00000). d6/b30 also passed at 1.000x with delta -0.00062 (t -3.42); d5/b30 and d5/b40 are
better trades at 1.01-1.02x. E.7b had nothing to recheck (no trust candidate on the shipped shape), and
E.6 found no ladder escalations in 1,200 games, so the skip allowance is the fleet default R=0.
`value_trust_depth` is unset, so no overnight tier was owed.

Shipped `decks/Red Elementals/Red Elementals.value.json` (as first committed in `ac857d69f`):
`value_play {ladder: single, leaf: none, alpha: relaxed, target_depth 6, budget_ms 20,
mull_gen_depth 1, mull_gen_budget_ms 3, expected_buckets 13}`.

**USER ruling 2026-11-17: d6/b30 replaces d6/b20.** The USER took the near-free E.7 trade above (`nlfit` at
d6/b30: 1.000x units vs the d5/20 default, delta -0.00062, t -3.42, 8 seeds x 1000 paired games), so the
sidecar now ships `budget_ms 30`. Red's eight d5 play rows in `test/regression_cases.sh` (smoke, the
2HG canary, regression, overnight) moved from b20 to b30, following the fleet convention that a deck's
d5 play rows run at its shipped budget (the d6/b20 commit had left them at b20; the suite pins d5, so
neither the sidecar's depth nor its budget reaches the suite on its own). The scoped smoke and regular run
at b30 was **byte-identical** on every row (0 slower, 0 faster, 0 play changes; 864 references, 0 drift;
561 scenarios). Search work rose (smoke d5 387k -> 411k units, 2HG canary 485k -> 540k), so the budget
does engage; on these seeds the extra search changes no decision. The accept refreshed only `job_cost.tsv`.
Trust plays no part: it was accepted at d5 for the staged learned model only, and `nlfit` (single
ladder, no leaf) has no escalation for trust to skip. Red's four overnight d5 rows (now b30) are left to the
periodic overnight run, as before.

**Shipping gate.** A scoped smoke and regular run (`--deck=redelem,redelem2hg`) found 0 slower games,
1 faster (regression d5 s3003, 4.3267 -> 4.3200), and 21 searched games whose play changed at the same
score. The strict viewer protocol passed 864 references with 0 drift. Attribution: on the same binary
with only the sidecar removed (`build/scratch/red_ship/`), all four changed cells reproduce the old GT
exactly, score and digest; with it, they reproduce the new run. Work units fell to 0.70x (smoke d5) and
0.66x (regression d5); d3 was 1.03x. Red's smoke and regular GT were accepted, and `check_gt_logs.py`
reports 1,252 consistent logs. Red's overnight rows move with the sidecar and are left to the periodic
overnight run.

The full pooled smoke run at freeze time showed 48 moved rows in other decks. They come from the
upstream mana-oracle commits after `c6027ae90`, which their owner has not accepted; they are not part
of this adoption.
