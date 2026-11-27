# Control-scoring mode, stage 2: the control score as a REPORT (build design)

Status: DESIGN (2026-11-27). The scoring library and the card classifier are built and tested (Python only).
The engine side (the schedule log) and the replay driver are specified here and not built. Parent design:
`docs/design/control-scoring-mode.md` (the "parent doc" below; its section numbers are written as §n).
The harness grammar this design uses is the PROTOTYPE's (`src/core/ControlInject.h`,
`scripts/control_inject_probe.py`). Every place that needs the stage-1 extension (cm-engine, to be documented in
`docs/design/control-inject-harness.md`) is marked **[EXT]**.

**Summary.**
* **Built:** `scripts/control_score.py` (the objective of §4: per-pair E, the two-level equal-weight mean,
  `P_g(W)`, `LB(t)`, the race-win share with and without each effect kind, and the horizon and censoring rules of
  Q5), `test/control_score_test.py` (`python3 test/control_score_test.py`, 69 checks, all pass), and
  `scripts/control_card_class.py` (which cards are control-based, derived from `cards.json`; section 2).
* **Stage 2 needs no search change.** The engine gets one executor-side log line, off unless a job asks for it.
  Python turns the log into effect schedules. ONE pooled batch replays each distinct schedule prefix through
  the gauntlet's fixed baseline seeds, and the library scores the pairs.
* **Pairing:** every `goldfish: true` suite deck at equal weight. Each deck gets 40 fixed held-out seeds
  (8,100,001 + i): 20 on the play and 20 on the draw. Each control game consults the 20 of the OPPOSITE
  parity. This replaces `pool[seed % 50]`, which the score never reads.
* **Three findings about the parent doc** (section 10): a slip in the §4.4 arithmetic (6.0 should be 4.0); the
  claim in §3.2/§5.4 that the passive opponent draws the gauntlet's own cards in order is false; and §4.2's
  survival step is pessimistic (the "chain" form, open question S1).
* **Cost is the main risk** (section 6). One schedule key costs at most 0.92 core-hours against the full
  gauntlet at its regression shapes. Screening with the baseline, early stopping and key dedup cut that a lot.
  Before any replay, a `plan` step prints the projection.
* **Open questions S1-S8** (section 11), each with the default taken.

## 1. What stage 2 computes

For each game of a control deck: its kill round W, its effect schedule `(T_k, kind, harness spec)`, and for
every gauntlet deck d and each of d's 20 opposite-parity baseline seeds s, the gauntlet game's kill round with
each schedule prefix applied. `control_score.py` then gives:

| quantity | function | definition |
|---|---|---|
| pair score | `pair_score(w, rounds, delays, form)` | `min_k max(T_k, W - D_k)`, `D = inf` for a lock (§4.1) |
| deck score | `two_level_mean` / `score_game` | mean over d's games, then equal-weight mean over decks (§4.1) |
| partial score | `deck_partial(w, rounds, delays_per_game)` | `P_d(W)` (§4.5) |
| search bound | `lower_bound(t, rounds, delays_by_deck)` | `LB(t) = mean_d P_d(t)`; no continuation scores below it (tested) |
| race-win share | `race_win(e, K, on_the_play, h)` | `K >= E` on the play, `K > E` on the draw, `E <= H` (§4.2, §4.6) |
| horizon | `censor_w`, `delay`, `unwon_share` | Q5: W unwon -> H+1 (H = 12..15); see below |

**Horizon and censoring (Q5).** The mapping and the baseline are built at 15. A deck's horizon H defaults to 12
and may be raised to 15 (`control_max_turns`). Its control games are run at `max_turns 15` and censored at H in
Python. One run then gives the score at H and the unwon share at 12 (validation V9 checks that running at 15 does
not change any game won by round 12).
* W: no kill by H -> `H + 1`.
* `D = K' - K` from the two kill rounds measured at 15. It may be negative (a disrupted gauntlet can re-plan into
  a faster line). The `k = 0` term then wins the min, so a negative D never helps.
* A delayed game that does not kill by 15 counts as a **lock**, `D = inf` (S2). `censored="bound"` uses the lower
  bound `16 - K` instead, a pessimistic variant.
* An undisrupted baseline game that does not kill by 15 gets **no credit**, `D = 0` (S2). No delay can be measured
  against a game that never threatened us.

**Two forms** (S1). `form="prefix"` is the §4.1 formula and the default. It reproduces §4.3a exactly (3 / 4 / 6 / 9;
mixes 3.5 and 4.65) and §4.4 (7.0). `form="chain"` is the exact race once each earlier prefix's delay is allowed
to keep us alive for a later effect: `E = min_k max(max_{j<=k}(T_j - D_{j-1}), W - D_k)`. It is never above the
prefix form. On §4.3a it gives 3 / 3 / 6 / 9 (mixes 3.0 and 4.35). Both forms satisfy every property in §4.3 and
the LB bound. The test checks each form against a direct simulation of its race condition on 5,000 random pairs.

**Race-win share (§5.6).** For each pair, our side wins the race iff `K >= E` (on the play) or `K > E` (on the
draw), where K is the undisrupted kill. The library reports it for each gauntlet deck: with the full schedule,
with no control (`E = W`), and with each effect kind removed. "Without kind X" is scored from the MEASURED
replays of the filtered schedule (its own keys, section 5). It is never the full schedule's delays with terms
deleted, because the delays are joint.

## 2. The control-based classification (Q11), with the per-deck output for the USER to correct

`python3 scripts/control_card_class.py [deck ...] [--markdown | --json]`. The classifier reads each card's
effect parameters, template, types and keywords. It never uses a hand list of names. Every finding has a
**kind**, a **role** and a **source**:
* **Role.** `primary` makes the card control-based: the guard in section 7 fires on it. `incidental` is a rider
  on a card whose goldfish job is the clock: burn that may also hit a creature, a threat's cast, attack or
  combat-damage trigger, a life-gain rider, or our enchantments becoming creatures. It is listed but does not
  gate.
* **Goldfish-credited.** Worldfire's exile-everything leaves the opponent no lands, hand or creature. The
  free-turn rule credits that from the opponent's real state, and the USER kept Worldfire in goldfish, so it does
  not gate either.
* **Sources:**
  * `param:` / `template:` / `keyword:`: the implementation.
  * `deck:`: two cards' parameters in the same deck. Under Enchanted Evening an enchantment-destroy is also land
    destruction. Enchanted Evening plus Opalescence makes every land a 0/0 that dies. Opalescence plus Parallax
    Wave is the exile-for-good loop. Damage to every creature in a deck with damage reflectors (Stuffy Doll) is
    that deck's clock, so it is demoted to incidental.
  * `oracle(inert)`: ONLY for `goldfish_inert` cards, which have no effect parameter. Their printed text, before
    the engine's `[note]`, is matched against fixed clause patterns.
* **Unmodelled clauses.** A printed control clause on a card that IS cast but whose implementation does not
  model that clause (Reality Spasm's tap mode, Magma Opus's tap) is listed as information.
* **Correction channel.** A USER correction goes into the card's own parameters as `"control_kind": [...]`
  (`[]` = none), which replaces the derived findings. No card sets it yet.
* **Harness column.** `proto:` is the prototype grammar; `ext:` is **[EXT]**.

Across the whole pool, 60 of 950 cards are control-based. 36 of 67 deck folders hold at least one, so the guard
reaches about half the decks (section 7).

**Enchanted Evening** (`decks/Enchanted Evening/Enchanted Evening.cod`): 6 control-based cards / 20 copies; 1 incidental-only card / 4 copies; 11 of 18 main-board cards have no finding.

| card | n | control-based | primary kinds | incidental kinds | source | harness |
|---|---|---|---|---|---|---|
| Enchanted Evening | 4 | **yes** | land_destruction/all, lock_piece | - | `deck:static_all_permanents_enchantments + static_animate_other_nonaura_enchantments_mv` | ext:wipe:T (every land); ext:lock:T (per lock rule) |
| Calming Verse | 3 | **yes** | sweeper/enchantments, sweeper/all_permanents, land_destruction/all | - | `param:destroy_opp_enchantments_then_own_if_untapped_land`; `deck:with static_all_permanents_enchantments (Enchanted Evening)` | ext:wipe_type:T:enchantment; ext:wipe:T (every permanent, lands included) |
| Sterling Grove | 4 | no | - | lock_piece | `param:grants_other_enchantments_shroud` | - |
| Opalescence | 4 | **yes** | land_destruction/all, lock_piece, lock_piece/wave_opal | blocker | `param:static_animate_other_nonaura_enchantments_mv`; `deck:static_all_permanents_enchantments + static_animate_other_nonaura_enchantments_mv`; `deck:fade_exile_target_creature + static_animate_other_nonaura_enchantments_mv` | ext:wipe:T (every land); ext:lock:T; ext:wave_loop:T |
| Force of Vigor | 4 | **yes** | removal/art_ench, land_destruction | - | `param:destroy_up_to_artifacts_enchantments`; `deck:with static_all_permanents_enchantments (Enchanted Evening)` | ext:nonland:T:i with an artifact/enchantment filter; proto:land:T:i |
| Parallax Wave | 4 | **yes** | removal/exile_until, lock_piece/wave_opal | - | `param:fade_exile_target_creature`; `deck:fade_exile_target_creature + static_animate_other_nonaura_enchantments_mv` | ext:exile_until:T; ext:wave_loop:T |
| Oblivion Ring | 1 | **yes** | removal/exile_until | - | `param:etb_exile_nonland_until_leaves` | ext:exile_until:T |

No finding: Utopia Sprawl, Wild Growth, Forest, Temple Garden, Exploration, Windswept Heath, Enchantress's Presence, Plains, Idyllic Tutor, Wooded Foothills, Sunpetal Grove.

**Hinata** (`decks/Hinata/Hinata.cod`): 4 control-based cards / 8 copies; 4 incidental-only cards / 13 copies; 15 of 23 main-board cards have no finding.

| card | n | control-based | primary kinds | incidental kinds | source | harness |
|---|---|---|---|---|---|---|
| Hinata, Dawn-Crowned | 4 | no | - | tax | `param:hinata_cost_reducer` | - |
| Crackle with Power | 4 | no | - | removal | `template:direct_damage` | - |
| Distorting Wake | 2 | **yes** | removal/bounce | - | `oracle(inert)` | ext:bounce:T |
| Icy Blast | 1 | **yes** | fog/tapdown | - | `oracle(inert)` | ext:tap:T:n |
| Soulfire Eruption | 4 | no | - | removal | `template:direct_damage` | - |
| Reality Spasm | 4 | no | - | - | - (unmodelled printed clause: "Tap X target permanents", fog) | - |
| Remand | 3 | **yes** | counter_to_hand | - | `param:counters_target_spell_to_hand` | ext:counter_to_hand:T |
| Memory Lapse | 2 | **yes** | counter_to_top | - | `oracle(inert)` | ext:counter_to_top:T |
| Magma Opus | 1 | no | - | removal | `template:direct_damage` (unmodelled printed clause: "Tap two target permanents") | - |

No finding: Forbidden Orchard, Izzet Boilerworks, Cascade Bluffs, Island, Sol Ring, Mountain, Mystic Monastery, Reflecting Pool, Expressive Iteration, Ornithopter of Paradise, Ponder, Preordain, Gamble, Irencrag Feat.

**Unpredictable Cyclone** (`decks/Unpredictable Cyclone/Unpredictable Cyclone.cod`): 2 control-based cards / 6 copies; 3 incidental-only cards / 6 copies; 17 of 22 main-board cards have no finding.

| card | n | control-based | primary kinds | incidental kinds | source | harness |
|---|---|---|---|---|---|---|
| Emrakul, the Aeons Torn | 4 | no | - | land_destruction | `param:annihilator` | - |
| Ulamog, the Ceaseless Hunger | 1 | no | - | removal | `param:cast_trigger_exile_target_permanents` | - |
| Ulamog, the Infinite Gyre | 1 | no | - | removal, land_destruction | `param:cast_trigger_destroy_target_permanents`; `param:annihilator` | - |
| Remand | 4 | **yes** | counter_to_hand | - | `param:counters_target_spell_to_hand` | ext:counter_to_hand:T |
| Toxic Deluge | 2 | **yes** | sweeper | - | `oracle(inert)` | proto:sweep:T |

No finding: Unpredictable Cyclone, Brainstorm, Street Wraith, Scalding Tarn, Island, Mountain, Steam Vents, Curator of Mysteries, Watery Grave, Cascade Bluffs, Myriad Landscape, Xander's Lounge, Ponder, Blood Crypt, Brainsurge, Long-Term Plans, Seething Song.

**Niv-Mizzet Combo** (`decks/NivMizzetCombo/NivMizzetCombo.cod`): 1 control-based card / 4 copies; 2 incidental-only cards / 6 copies; 16 of 19 main-board cards have no finding.

| card | n | control-based | primary kinds | incidental kinds | source | harness |
|---|---|---|---|---|---|---|
| Niv-Mizzet, the Firemind | 4 | no | - | removal | `param:draw_trigger_damage` | - |
| Niv-Mizzet, Parun | 2 | no | - | removal | `param:draw_trigger_damage` | - |
| Remand | 4 | **yes** | counter_to_hand | - | `param:counters_target_spell_to_hand` | ext:counter_to_hand:T |

No finding: Curiosity, Tandem Lookout, Island, Cascade Bluffs, Ponder, Izzet Signet, Sol Ring, Generator Servant, Saprazzan Skerry, Sandstone Needle, Preordain, Spirebluff Canal, Brainstorm, Gitaxian Probe, Ferrous Lake, Sulfur Falls.

**Worldfire** (`decks/Worldfire/Worldfire.cod`): 1 control-based card / 2 copies; 1 goldfish-credited card / 4 copies; 1 incidental-only card / 4 copies; 19 of 22 main-board cards have no finding.

| card | n | control-based | primary kinds | incidental kinds | source | harness |
|---|---|---|---|---|---|---|
| Worldfire | 4 | no (goldfish-credited) | sweeper/all_permanents, land_destruction/all, discard | - | `param:exile_all_permanents_hands_graveyards` | ext:wipe:T; proto:bigdisc:T |
| Rift Bolt | 4 | no | - | removal | `template:direct_damage` | - |
| Memory Lapse | 2 | **yes** | counter_to_top | - | `oracle(inert)` | ext:counter_to_top:T |

No finding: Mountain, Island, Steam Vents, Lotus Bloom, Seething Song, Mercadian Bazaar, Mystical Tutor, Ponder, Scalding Tarn, Keldon Halberdier, Brainstorm, Flooded Strand, Sandstone Needle, Saprazzan Skerry, Personal Tutor, Overmaster, Irencrag Feat, Mistrise Village, Preordain.

(Sideboard cards are not classified. Hinata's and Cyclone's sideboards hold cards that are not in `cards.json`.)

## 3. The executor-side schedule log (engine; no search change)

**What the engine does.** A batch job may carry `"ctrl_log_cards": {"<card name>": ["<kind>", ...], ...}`, written
by the replay driver from the classifier (all classified cards, primary and incidental). With the field absent
nothing changes. With it present, the REAL game's executor writes one stderr line per event of a listed card.
Rollouts and mulligan trials never log; the guard is the same as ctrl-inject's (`InRolloutForCtrlInject`). The
engine does no classification and makes no decision. Play is byte-identical with and without the field (V2).

    [ctrl-sched] job=<name> gi=<i> seed=<s> otp=<0|1> round=<turn_number> side=<own|opp> card=<name>
                 num=<m_number> event=<resolve|enter|leave|activate> target=<self|opp|none> detail=<...>

* `resolve`: an instant or sorcery resolving (Calming Verse, Force of Vigor, Toxic Deluge).
* `enter` / `leave`: a listed permanent's presence interval (Parallax Wave, Opalescence, Enchanted Evening,
  Oblivion Ring). Static and lock effects are derived from these intervals.
* `activate`: a listed permanent's activation (each Parallax Wave fade-counter exile). `target` says whose
  object it hit in OUR game. The replay chooses its own targets, so this is a census, not an instruction.
* A `target=self` event is logged and then ignored by the schedule builder: it is not an effect on the opponent.
  Remand is modelled as self-targeted only, so in stage 2 a Remand is never an effect (S7).
* W comes from the job's `.wins` file. Nothing new is needed for it.

**The credited round is our game's `turn_number` when the event happens, on the play and on the draw.**
`GameEngine::RunTurnFrom` advances the counter only at our turn start, so on the draw the opponent's turn after
our turn t still runs under `turn_number == t`. That is exactly §4.6's rule: the opponent's own turn t is
credited to round t on the play and to round t - 1 on the draw. **The injection turn in the gauntlet game** is
`turn_number + (0 if we are on the play else 1)`, for our-turn and opponent-turn events alike. On the play, our
turn t comes before the gauntlet's turn t. On the draw, the gauntlet's turn t comes first, so our round-t effect
first touches its turn t + 1. The prototype applies a board or hand effect at the start of the gauntlet's turn
(an "opponent's sorcery on its preceding turn") and a counter during the turn, which matches both timings. An
instant aimed at the gauntlet's combat (a tap-down, a fog, flash removal) needs a mid-turn application point
**[EXT]**.

**Log -> schedule** (`scripts/control_replay.py schedule`, to build). For each game:
1. Turn each event into effects through the classifier's harness mapping. Use primary findings by default;
   `--with-incidental` adds the riders. The deck-context lock rules build effects from presence intervals:
   * `wave_loop` from round `max(enter(Wave), enter(Opalescence))` while both are present and at most one Sterling
     Grove is ours (§4.3a), **[EXT]**;
   * a land wipe from `max(enter(EE), enter(Opalescence))`;
   * a full wipe on a Calming Verse resolving under EE.
2. Drop every effect with `T_k >= W`. Its term is at least `T_k >= W`, so the drop is exact in both forms.
3. Write the canonical key of each prefix: `<gauntlet parity>|<spec;spec;...>` with gauntlet injection turns,
   for example `play|exile_until:4:0;wave_loop:5`. Targets are chosen by policy inside the replay (Q3/Q8), so
   the key holds no card names.

**What stage 2 can and cannot show (S7).** The control deck plays its normal goldfish game, so it casts only what
a kill-seeking search chose. `goldfish_inert` cards (Memory Lapse, Toxic Deluge, Distorting Wake, Icy Blast) are
never cast, and Remand only ever targets our own spell. So Hinata, Unpredictable Cyclone, Niv-Mizzet Combo and
Worldfire will show `E == W`, or close to it, at stage 2. Their control value arrives with stage 3 and §5.7.
Enchanted Evening is the only one of the five decks with a non-trivial stage-2 schedule, which is why it goes
first (§7).

## 4. The pairing

* **The gauntlet** is every deck that is `goldfish: true` (`deck_registry.goldfish_enabled`) and is a suite deck
  (`test/regression_cases.sh`), at equal weight (Q2 and §7.5). The control deck itself is included when it is a
  goldfish deck, so Hinata faces Hinata (S6).
* **Fixed seeds.** For each gauntlet deck: `seed = 8,100,001 + i` and `game_index = i`, i = 0..39. The tiers use
  seeds 777,000 to 2,000,000 and the probe used 7,900,001, so these are held out. The same seed set serves every
  gauntlet deck. `on_the_play = (seed % 2 == 0)`, so each deck has 20 games on the play and 20 on the draw.
* **Opposite parity (§4.6) needs no engine change.** A control game on the play (even seed) consults the
  gauntlet's 20 odd seeds (gauntlet on the draw), and a game on the draw consults its 20 even seeds. Each control
  game therefore sees 20 games per gauntlet deck, the USER's minimum (§7.5).
* **`pool[seed % 50]` is replaced, not extended.** Our game's passive opponent stays whatever the deck already
  deals, and the score never reads it. In particular, a discard in our game never names the card that is
  discarded in the replay: the replay's own target policy picks it (Q3). See section 10, finding 2, for why the
  original pairing could not do this anyway.

## 5. The baseline table and the replay manifest

**Baseline (built once, shared by every control deck and every key; USER §7.5).** One pooled batch: every gauntlet
deck x 40 seeds, `max_turns 15`, at the deck's shipped play setting (its profile, value sidecar and keep table, as
in its tier rows). Stored for each deck in `logs/control-mode/baseline/<deck-slug>.json`. It is regenerable, but
it costs core-hours, so it lives on the host drive. The in-flight scratch goes in `build/scratch/control-mode/`.

    {"format": "ctrl-baseline-v1", "deck": "Burn", "deck_dir": "decks/Burn",
     "stamp": {"decklist_sha256": "...", "profile_sha256": "...", "value_sha256": "...|null",
               "keeptable_sha256": "...|null", "src_tree": "<git rev-parse HEAD:src>",
               "harness": "<ControlInject grammar version>", "seed_base": 8100001, "n": 40,
               "max_turns": 15, "depth": 5, "budget_ms": 20},
     "games": [{"gi": 0, "seed": 8100001, "otp": false, "k": 5, "digest": "66921a2e...",
                "dmg": [0, 3, 8, 14, 20]}, ...]}

* `k` is the kill round (null if there is no kill by 15). `digest` is the per-game digest from `.wins`.
* `dmg` is the cumulative damage to its passive opponent at the end of each round. It screens life-gain replays
  (§5.6), and it needs one engine log line **[EXT]**.
* **Invalidation:** any change in the stamp makes that deck's baseline, and every replay cell of that deck, stale.
  Other decks are untouched. A rebuilt baseline lists the seeds whose digest changed. A cell stays strictly keyed
  on `src_tree`: an unchanged baseline does not prove an unchanged disrupted game, because the disrupted game runs
  code the baseline never reaches (S8).

**The mapping cache (level 1 of §7.5: the full joint key, as a cache of the sequences the deck produces).**
`logs/control-mode/mapping/<deck-slug>.jsonl`, one line per measured cell:

    {"key": "draw|exile_until:4:0;wave_loop:5", "gi": 7, "seed": 8100008, "k_ctrl": null,
     "censor_at": 11, "applied": [1, 1], "stamp": {...the baseline stamp...}}

`censor_at` is the `max_turns` the cell ran at. `k_ctrl = null` means no kill by then.

**Which cells are needed.** For each control game (parity p, W, schedule), each prefix k, each gauntlet deck d,
and each of d's 20 seeds s of the opposite parity, the cell is needed iff:
1. `inject_turn(effect 1) <= K_s`. Otherwise the gauntlet killed before the first effect, so `D = 0` exactly and
   there is nothing to run. (V6 samples skipped cells to confirm this.)
2. It is not already cached with an adequate `censor_at`.

**Early stop.** A cell's `max_turns` is `min(15, K_s + max over its consumers of (W - T_k))`. Past that round
every consumer's term has already reached its floor `T_k`. An unwon cell is then a lock, `delay = inf`, for those
consumers, which is exact for each of them. A later consumer that needs more re-runs the cell at a higher cap.

**The replay manifest: ONE pooled `mtg --batch`** over every needed cell of every key of every deck (the
pooled-queue rule; never one batch per deck or per key):

    {"jobs": [{"name": "Burn__3f9a1c__7", "deck": "decks/Burn/Burn.txt", "profile": "decks/Burn/Burn.profile.json",
               "seed": 8100008, "game_index": 7, "games": 1, "max_turns": 11,
               "ctrl_inject": "exile_until:4:0;wave_loop:5"}, ...]}

* One job per cell works with the prototype today (the probe used per-arm jobs).
* A `"seeds": [...]` list, or a per-game `ctrl_inject`, would cut the job count **[EXT, optional]**.
* The driver (`scripts/control_replay.py`, to build) has five steps: `plan` (count the cells, project the core-hours
  from `test/suite_cost.json`, print), `baseline`, `make`, `ingest` (`.wins` + `[ctrl-inject]` lines -> cache) and
  `assemble` (the cache plus our games -> the results document `control_score.py score` reads; its shape is in
  that script's docstring).
* The "without kind X" keys are generated by the same planner from the filtered schedules.

## 6. Prefix sharing and cost

**Sharing built in at no engine cost:**
* Every prefix of one schedule is itself a key. Keys form a trie shared by all control games, so a key measured
  once serves every game that produced it.
* Baseline screening (cell rule 1).
* The early-stop cap.
* Dropping effects at or after W.

**Sharing that needs the engine [EXT, measure before relying on it]:** in-process snapshots. For each seed, run
the baseline to the first injection turn, copy the state, and run each child key from the copy. This saves the
pre-effect rounds of every replay, typically 2-4 of about 5. It needs no cross-process resume.

**The numbers** (`test/suite_cost.json`: `cost_per_game_ms` is each deck's WORST regression shape, d5 and 2HG
included, so these are upper bounds):
* One game of every gauntlet deck costs 165.5 core-s. The five most expensive decks are 100 core-s of that:
  Beasts 33.7 s, Hinata 29.8 s, Gods 16.6 s, Worldfire 10.3 s and Five Colour 9.8 s.
* **Baseline, once:** 40 x 165.5 s = 1.84 core-hours.
* **One key** (20 opposite-parity seeds per deck): at most 20 x 165.5 s = **0.92 core-hours**. Screening scales
  this by the share of gauntlet games still alive at the key's first injection turn (§3.1):

  | first injection turn t | 3 | 4 | 5 | 6 | 7 |
  |---|---|---|---|---|---|
  | share replayed, P(no kill by end of t-1) | ~1.0 | 0.90 | 0.48 | 0.15 | 0.03 |
  | core-hours per key (upper bound) | 0.92 | 0.83 | 0.45 | 0.14 | 0.03 |

* **The control games themselves,** per game at the worst shape: Hinata 29.8 s, Unpredictable Cyclone 3.2 s,
  Niv-Mizzet Combo 1.75 s, Worldfire 10.3 s. Enchanted Evening is not in the suite; its 224-game step-8c A/B took
  about 4.5 minutes of batch wall at d3 b10.
* **Per deck:** `N_ctrl x c_own + sum over needed cells of c_d`. The number of distinct keys is unknown until the
  log exists, which is why `plan` runs first. As an illustration only: if 200 Enchanted Evening games produce
  about 300 distinct prefix keys, mostly first-injected on rounds 3-4, the bound is about 250 core-hours at worst
  shapes. That is one long big run. The levers, in order:
  1. coarse keys (kind + turn + policy, already the default);
  2. snapshots;
  3. running the gauntlet at d3 b10 for the mapping, roughly 10-30x cheaper (the probe measured 0.11 s per game on
     five cheap decks; S5);
  4. fewer report games;
  5. scoring the rarest keys with the level-4 additive fallback, with its share disclosed.

## 7. The report layout

`python3 scripts/control_score.py score <results.json> [--horizon H] [--form prefix|chain] [--json]` prints the
core, and the stage-2 driver adds the census lines. A real report contains:

    control score: Enchanted Evening  (form=prefix, horizon 12, unwon = 13), 200 control games
      gauntlet: 63 goldfish decks x 20 opposite-parity seeds, baseline stamp <src_tree> / <date>
      E 6.412 +- 0.06   beside raw W 9.880   (E - W -3.468)        [also form=chain: E 6.21]
      unwon share: 61.0% at 12, 61.0% at H=12, 48.5% at 15

      gauntlet deck                  E_d   base K   race win   no ctrl
      Breaching Dragonstorm        8.940     3.10      3.0%      0.0%
      Burn                         7.105     4.31     22.5%      0.5%
      ...

      without one effect kind (measured replays of the filtered schedule):
        -lock_piece     E 8.10 (+1.69)   race win 6.2% (all-deck mean)
        -removal        E 7.02 (+0.61)   race win 18.0%
        -life_gain      ...              (the first effect checked for over-rating, design 5.6)

      effects per game by kind and round; share dropped at or after W; cells replayed / screened / cached;
      censored cells (lock at the cap) share

Rules:
* E is always printed beside the raw W (Q1).
* The per-deck partials E_d are always printed, so the burn / combo / creature split stays visible (§4.5).
* The race-win share is printed with and without control. An effect kind that moves E but not the race-win share
  is the over-rating signal of §5.6.
* The unwon share at 12 is printed for every deck, and is the USER's evidence for raising a deck's horizon (Q5).
* The numbers in the block above are invented placeholders that show the layout. `test/control_score_test.py`
  exercises the real code path on a synthetic file.

## 8. The `deck_compare.py` guard and the mode split (Q11)

**The guard** (to build with stage 2): a `control_guard(spec)` step right after `preflight()`.
1. Classify every card whose count differs between any two arms (added, removed or changed). Use
   `control_card_class.classify_card` and the deck-context rules over the arm's own counts. That needs a
   counts-based entry point beside `classify_deck(path)`, a three-line wrapper.
2. **If any changed card is control-based** (primary, not goldfish-credited):
   * the deck's modes marker has `control_score: true` -> run ONLY the control-scored mode (1v1 too, once it
     exists). No goldfish games: their compute goes to a larger control sample (USER: "we know it will be totally
     unreliable"). The report says: `control card in the swap: <card> (<kinds>) -> control mode decides; goldfish
     not run`.
   * otherwise -> **refuse**, in the same way as `refuse_on_cards`:

         this swap adds/removes control-based card(s) <card> (<kinds>, from <source>); a goldfish-only verdict is
         not valid (USER Q11). Mark the deck "control_score": true in decks/<Name>/<Name>.modes.json and give it
         control-mode suite rows, then re-run. If the classification is wrong, correct the card's
         "control_kind" in cards.json.

     There is no spec-level override (S4).
3. **No control-based card in the swap:** the goldfish screen decides at its usual size. A deck that also has
   `control_score: true` runs a smaller control sample as a sanity check (default: 200 control games, both
   arms on the same games). A check that points the other way beyond its own noise (`|delta| > 2 se`, opposite
   sign) is reported to the USER and does not override the verdict.
4. The report always lists the classification of every changed card (kinds, role, source), so the USER can
   correct it, and states which of the three cases applied and why.

**Reach:** 36 of the 67 deck folders hold at least one control-based card (section 2). A goldfish-only deck that
screens one of those cards in or out is refused until it opts into the mode. Swaps of incidental cards (burn,
threats with triggers, life-gain riders) keep their goldfish verdict.

**Modes per deck** (Q11 answer, as the stage-2 consumers read them):

| marker | goldfish rows | control rows | screens |
|---|---|---|---|
| no marker / `goldfish: true` | yes | no | goldfish; a control-card swap is refused |
| `goldfish: true, control_score: true` | yes | yes | no control card in the swap: goldfish decides, control sanity check. Control card: control only |
| `goldfish: false, control_score: true` (Enchanted Evening) | no | yes | control only |

Enchanted Evening's marker does not carry `control_score: true` yet, and the deck has no play profile (S3).

## 9. Validation (§7 stage 2, made concrete)

| # | check | how | status |
|---|---|---|---|
| V1 | no control effect -> `E == W` exactly, per game | unit test; end to end, a deck with no control-based card (31 decks qualify, e.g. Stompy Surprise) produces empty schedules and its report prints `E - W = 0.000` | unit: PASS |
| V2 | the schedule log changes nothing | per-game digests of Enchanted Evening's games identical with and without `ctrl_log_cards`; smoke byte-identical (the field is absent) | to run |
| V3 | a known lock -> `E == L` | unit test; end to end, a hand-built `skip:L;skip:L+1;...;skip:15` schedule (a lock made of the prototype's `skip`) must leave every replay unwon, so `D = inf` and `E = L` exactly, per game | unit: PASS |
| V4 | monotonicity | unit property tests (floor, `E <= W`, in W, in D, a later effect); end to end, a schedule's measured E never rises when an effect is appended, and its prefix cells are bit-identical to the shorter schedule's cells (determinism) | unit: PASS |
| V5 | `LB <= E` for every continuation | unit property test, both forms, 24,000 continuations each | PASS |
| V6 | screening is exact | run a sample of screened-out cells anyway; each must give `k_ctrl == K_s` | to run |
| V7 | the early-stop cap is exact | re-run a sample of capped cells at 15; every consumer's term is unchanged | to run |
| V8 | parity and timing | every pair has opposite `otp`; an instant logged on the opponent's turn on the draw is credited `turn_number` (= their turn - 1) and injected on their own turn | to run |
| V9 | the horizon run at 15 equals 12 for won games | a sample of control games at `max_turns 12` and 15: every game won by round 12 has the same W and digest | to run |
| V10 | baseline determinism | building the baseline twice gives identical digests | to run |
| V11 | `skip` analytic check on the baseline seeds | +1.000 for every game not won before the skipped round (the probe's check, with the "your next turn" fix of §8.3) | stage 1 |
| V12 | Enchanted Evening first | E against raw W, and how much comes from the round-3 Wave alone (E with only the terms up to the first Wave) | to run |
| V13 | the classification | the USER reviews section 2 | open |

## 10. Findings about the parent design doc

1. **§4.4 arithmetic.** The "average D first, capped at 12" row prints 6.0. With its own numbers (mean D = 6) it is
   `max(4, 10 - 6) = 4.0`, the same as the uncapped row. The conclusion stands: per pair gives 7.0, and averaging
   first gives 4.0. The test asserts 7.0 and 4.0.
2. **§3.2 / §5.4: the passive opponent does NOT draw the gauntlet deck's own game.**
   * In our game, `opponentdeck::Deal` shuffles its library with `Shuffle(SaltSeed(seed, 0xDEC7))`, numbers the
     cards from 100000, and deals a flat 7 with no mulligan.
   * The gauntlet's own goldfish game shuffles with `ShuffleByKey(SaltSeed(seed, shuffle_salt_opening))` on its
     own numbering, and it mulligans.
   * So the same seed does not give the same cards in the same order, and "the same card can be removed from the
     paired gauntlet game" does not hold. Stage 2 does not depend on it: the pairing is the fixed baseline seeds,
     and hand effects choose their targets inside the replay.
3. **§4.2: the survival step is pessimistic.** "We survive to T_k" is written as `K >= T_k`. After prefix k-1 the
   gauntlet kills on `K + D_{k-1}`, so the exact condition is `K + D_{j-1} >= T_j` for every j <= k. Solving the race
   with it gives the chain form of section 1, which is never above the §4.1 form. In §4.3a it lowers row 2 from 4 to
   3: the Wave's 2-round delay keeps us alive to cast Opalescence on round 4. Both forms pass every §4.3 property,
   and the USER's lone-effect floor holds in both, because the floor concerns a lone effect and the chain form
   differs only with earlier effects. The test checks each form against a direct simulation of its race. Which one
   is "<win turn - average control impact>" is the USER's call (S1; it extends Q9).

## 11. Open questions (each with the default taken; nothing waits on them)

* **S1. Which form of the pair score?** The §4.1 "prefix" form, or the survival-chained form (finding 3)?
  Default: prefix, because it is the parent doc's formula and the more conservative of the two. Every report
  prints both E values.
* **S2. Censored games.** A disrupted gauntlet game that does not kill by 15 is scored as a lock (`D = inf`). An
  UNDISRUPTED game that does not kill by 15 gives no credit (`D = 0`). Default: as stated. The pessimistic
  `censored="bound"` variant (`D = 16 - K`) is available. The defaults are confined to games with no kill inside
  15 rounds. Q5's raised horizon is what makes those rare.
* **S3. Enchanted Evening's set-up.** Its marker has no `control_score: true`, and the folder has no play profile
  (only the `.cod` and the marker). Default: add `"control_score": true` to the marker, and generate a play
  profile with `scripts/analyze_deck.py` before stage 2. Per Q6 it gets no keep table and no value leaf.
* **S4. Classification corrections.**
  * Life gain is `primary` only on a pure life-gain instant or sorcery (Feed the Clan, Heroes' Reunion), per the
    Q11 list. This means a life-gain deck's screen of those cards is refused until the deck opts in.
  * Riders are incidental: burn that may hit a creature, the big threats' cast and attack triggers, Hinata's tax,
    life gain on lands and creatures.
  * Worldfire is goldfish-credited and does not gate.
  * Blasphemous Act and Chain Reaction are demoted in Stuffy Doll.
  * There is no spec-level override of the guard.

  Default: as listed. A correction goes into `cards.json` as `control_kind`.
* **S5. The gauntlet's settings for the mapping.** Its shipped play settings, or d3 b10? Default: shipped settings
  (§7.5's "at play settings"). `plan` prints the projection before any replay, and d3 b10 is the first lever if
  the projection is out of reach.
* **S6. Does a control deck face itself?** Default: yes, every `goldfish: true` deck is in the gauntlet, so Hinata
  faces Hinata.
* **S7. The stage-2 gap.** With no search change, the decks whose control cards are `goldfish_inert` or
  self-targeted (Hinata, Cyclone, Niv-Mizzet Combo, Worldfire) score `E == W` or near it. Default: report them
  anyway as the stage-2 baseline, so that the stage-3 gain can be measured. Enchanted Evening is the first deck
  whose stage-2 E means anything.
* **S8. Cache invalidation.** Default: strict, with every cell keyed on the `HEAD:src` tree. A
  baseline-digest-carry with sampled verification (deck_compare's carry pattern) could keep cells across engine
  changes that do not move play. It is not built.

## 12. Dependencies on the harness extension (cm-engine)

Needed for stage 2 on Enchanted Evening: `exile_until` (with the return round from the fade counters, the return
as new objects, and tokens not returning), `wave_loop`, `wipe` / `wipe_type`, `nonland` with a type filter, the
per-round damage line for the baseline, the `ctrl_log_cards` job field with its `[ctrl-sched]` line, and the
counter target policies of Q3/Q8.

Needed for the other decks (stage 3 onward, since stage 2 never casts these): `counter_to_hand`,
`counter_to_top`, `bounce`, `tap`, `lifegain`, `fog`, `tax`, `blocker` (§5.6), and a mid-turn application point.

Optional: a `seeds` list per job, and in-process snapshots. The prefix-replay driver is designed to work with
one job per cell on the prototype grammar until these exist.
