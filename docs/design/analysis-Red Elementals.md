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

## Stage 5 — verification
(not yet run)

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
