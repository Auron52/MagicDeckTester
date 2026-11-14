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
| Nova Chaser | 3 | CHAMPION an Elemental (new); post-combat main relevance | pending |
| Thunderkin Awakener | 3 | attack trigger: gy Elemental (toughness < own) tapped+attacking, sac next end step (new) | pending |

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

## Stage 5 — verification
(not yet run)

## Approved deferrals
(none yet)

## Open questions for the user
- PROVISIONAL (Hellspark Elemental, unearth): the "exile it if it would leave" replacement is routed
  only through the shared sacrifice chokepoint (and the end-step exile sweep); destroy effects, the
  lethal-damage / toughness-0 SBA, the legend rule, creature sweeps and bounce are NOT routed. None can
  befall an unearthed creature in this list (passive opponent, nothing of ours damages/shrinks/bounces
  our creatures). Sign-off owed.
- PROVISIONAL (Sunspine Lynx): both statics are modelled at every site the engine models lifegain /
  prevention, but are inert here (no lifegain or prevention in the list or from the opponent).
- Viewer audit INERT_PARAMS additions (need the user's OK): `etb_damage_each_player_per_nonbasic_land`,
  `static_players_cant_gain_life`, `static_damage_cant_be_prevented` (no player choice).
