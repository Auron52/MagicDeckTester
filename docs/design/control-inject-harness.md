# Control-effect injection harness (control-scoring mode, stage 1)

The interface spec for the `ctrl_inject` / `ctrl_record` batch fields. It is written so that a driver
(the gauntlet baseline table, the per-(effect, round) delay table, the clairvoyant target sweeps) can be
built from this page alone. Design context: `docs/design/control-scoring-mode.md` (sections 4, 5.6, 5.7,
7 stage 1, 7.5, 8). Code: `src/core/ControlInject.{h,cpp}`, hooks in `src/core/GameEngine.cpp`, the job
fields in `src/runner/BatchRunner.cpp`, the tax in `src/ai/ManaPayment.cpp` (`CtrlInjectSpellTax`), three
`GameState` bytes (`ctrl_skipped_turns`, `ctrl_tax_all`, `ctrl_tax_split`). Tests:
`test/unit/test_control_inject.cpp`.

## 1. What it does

A batch job may carry a schedule of CONTROL EFFECTS. Each one disrupts the GOLDFISHING deck (the deck the
job plays; in the control mode, a gauntlet deck) as if the passive opponent (the control deck) had cast a
control spell: destroy, exile, exile-until, bounce, tap down, discard, tax, life gain, counter (hard / to
hand / to library top), or take the goldfisher's whole turn (skip). The goldfisher's search is never told
in advance. After an effect changes anything, the real game drops its committed line and re-searches from
the disrupted state, so the goldfisher re-plans as a real player would. Effects apply in the REAL game only,
never in a mulligan / bottoming trial or a rollout (those run on the same engine with `m_in_rollout` set,
`AIEngine::InRolloutForCtrlInject`).

**Default off.** A job without `ctrl_inject` and without `ctrl_record` reaches none of the hooks, prints
nothing, and plays byte-identically (section 9).

## 2. Job fields

| field | type | default | meaning |
|---|---|---|---|
| `ctrl_inject` | string | `""` | the effect schedule (grammar in section 4). A parse error aborts the manifest with `ctrl_inject: "<effect>": <why>`. |
| `ctrl_record` | bool | `false` | print one `[ctrl-game]` line per game: the damage curve, the kill kind (section 6.2). Effects need not be present: `ctrl_record` alone is how the no-control BASELINE table is recorded. |
| `max_turns` | int | 8 | the horizon. The mapping horizon is 15 (Q5 ruling); 15 is tested (section 9). An unwon game's `.wins` turn is `-1`; score it as `max_turns + 1`. |

Every other job field works as usual (`deck`, `profile`, `games`, `seed`, `game_index`, `depth`,
`budget_ms`, `ignore_play_profile`, ...). Give every arm its own job `name`: `--resume` and the `.wins`
file key on the name.

Run the batch exactly as any other: `build/Release/mtg --batch <manifest> --threads N --game-log-dir <dir>
[--resume]` (small runs through `scripts/box_lane.sh --slots 2 -- ...`). Per-game kill turns land in
`<dir>/<job>.wins` (`<gi> <wt> <digest>` per line, `wt <= 0` = unwon). The effect log and the per-game
records go to the batch's STDERR, one line each, prefixed `[ctrl-inject] ` / `[ctrl-game] ` and followed by
one JSON object. Redirect stderr to a file and filter on the prefix.

**Pairing.** Arms of one gauntlet deck that share `seed` and `game_index` play the same shuffles, so game
`gi` of an arm is paired with game `gi` of the base arm: their draws agree up to the first effect. The
delay of one game is `wt(arm) - wt(base)` (unwon = `max_turns + 1`).

## 3. Timing: rounds, and who acts when

Rounds are the engine's ROUNDS (`GameState::turn_number`, the unit of every reported win turn; extra turns
and free turns do not advance it). The control player is taken to be ON THE PLAY in every round: its turn
of round t comes before the goldfisher's turn of round t. So, for an effect of round t:

| kind | applies | in real-Magic terms |
|---|---|---|
| `destroy`, `exile`, `exile_until`, `bounce`, `discard`, `life`, `skip`, `tax` (start / end) | at the START of the goldfisher's turn of round t, after the round counter advances and before its untap step | a sorcery / main-phase effect on the control player's turn t |
| `exile_until` returns | the same point of round `ret`, BEFORE that round's new effects | the source leaving at the control player's upkeep of round `ret` (Parallax Wave fading out) |
| `tap` | after the goldfisher's untap step of round t, before its upkeep | Rishadan Port / Icy Manipulator at its upkeep: the permanent is unusable that turn |
| `counter`, `counter_to_hand`, `counter_to_top` | as a goldfisher spell of round t is about to resolve | a counterspell cast in response (the control player waits until the spell is on top, so the spell's cast triggers have already resolved) |

The hooks run only at the first turn of each new round (never inside a goldfisher extra turn or a free
turn of the same round). A counter of round t can hit a spell in any goldfisher turn of round t, including
an extra turn.

An effect whose round is never reached (the game was won earlier) does nothing and prints nothing.

## 4. Grammar

    spec     := effect { ';' effect }               whitespace around fields is ignored
    effect   := kind ':' rounds { ':' field }
    rounds   := R | R '-' R2                         1 <= R <= R2; a range repeats the effect every round
    field    := class | selector | amount | 'ret=' R

* **class** (which objects are candidates): `perm` (any permanent; the default for board kinds), `land`,
  `creature`, `nonland`, `artifact`, `enchantment`, `noncreature`, `card` (any card / spell; the default
  for `discard` and the counters). A creature is `IsCreature()` or an animated permanent.
* **selector** (which candidate; default `0`):
  * `<i>`: the i-th candidate (0-based). An index at or past the candidate count is a NO-OP, logged
    `applied: 0`, so a sweep over `i = 0 .. count-1` is exact. This is the clairvoyant target choice of
    Q3 / Q8: run one arm per index and keep the one with the largest delay.
  * `big`: creatures: the largest effective power; any other class: the highest mana value; ties: the
    first. Not allowed for counters (the round's later spells are unknown).
  * `mv>=N`: the first candidate whose mana value is at least N.
  * `name=<Card Name>`: the first candidate with exactly that name. Must be the LAST field (the name runs
    to the end of the effect and may contain `:`).
  * `all`: every candidate (a sweeper, Mind Twist, a lock on every spell).
* **amount**: a bare integer for `tax` and `life` (required); for every other kind a bare integer is an
  index selector.
* `ret=R`: `exile_until` only, required, `R > R2`.

**Kinds**

| kind | fields | |
|---|---|---|
| `skip` | none | the goldfisher takes no turn in the round(s) |
| `destroy` | class, selector | |
| `exile` | class, selector | for good |
| `exile_until` | class, selector, `ret=R` | |
| `bounce` | class, selector | to its owner's hand |
| `tap` | class, selector | candidates: UNTAPPED permanents |
| `discard` | class, selector | hand cards |
| `tax` | amount, optional `all` / `noncreature` / `creature` | `{amount}` more per spell |
| `life` | amount | the passive opponent gains `amount` |
| `counter` | class, selector | hard counter: graveyard |
| `counter_to_hand` | class, selector | Remand |
| `counter_to_top` | class, selector | Memory Lapse |

**Aliases** (the prototype probe's arms read the same): `land:T[:i]`, `creature:T[:i]`, `nonland:T[:i]` =
`destroy:T:<class>:i`; `bigcrt:T` = `destroy:T:creature:big`; `sweep:T` = `destroy:T:creature:all`;
`discard:T:i` = `discard:T:card:i`; `bigdisc:T` = `discard:T:nonland:big`. **Changed from the prototype:**
a counter's bare integer is now a candidate INDEX; the prototype's `counter:T:m` (first spell of mana value
>= m) is now `counter:T:mv>=m`, and `counter:T` is the first spell of the round.

Examples:

    skip:3                                   the goldfisher loses its round-3 turn
    skip:4-6                                 a three-round lock
    destroy:3:creature:1                     the second targetable creature, round 3 (Doom Blade sweep: i = 0..count-1)
    exile_until:3:creature:all:ret=6         Parallax Wave on round 3 taking everything, fading at round 6
    tap:2-5:land:0                           Rishadan Port on the first untapped land, rounds 2..5
    tax:2-99:1:noncreature                   Thalia from round 2 on
    life:4:3                                 we gain 3 on round 4
    counter_to_hand:3:mv>=2                  Remand the first spell of mana value >= 2 on round 3
    counter_to_top:4:name=Lightning Bolt     Memory Lapse aimed at a Bolt on round 4
    land:3:0;land:4:0                        Stone Rain twice (prototype alias)

## 5. Semantics of every kind

Candidates are always the GOLDFISHER's objects (its permanents, its non-staged hand, its spells); the passive
opponent's own permanents and spawns are never touched. Every board change goes through the engine's own
zone-change route, so the goldfisher's watchers see it as the rules require.

**Targeting.** A single-target selector (index, `big`, `mv>=`, `name=`) TARGETS: a permanent with shroud or
hexproof (`PermanentHasShroud`, `Keyword::Hexproof`; the control player is an opponent, CR 702.11b /
702.18a) is not a candidate. `all` does not target and takes them too. Not modelled: protection from a
colour (the effect has no colour), ward (its cost is taken as paid), "can't be the target of" riders other
than hexproof / shroud.

* **destroy**: one simultaneous destroy event (`DestroyRemoveTogether` + `DestroyFireTogether`). An
  indestructible permanent stays (CR 702.12b; outcome `indestructible`). Umbra armor (CR 702.89a) applies:
  the Aura is destroyed instead and the creature's damage is removed (outcome `umbra armor: <Aura>
  destroyed instead`). An unearthed permanent is exiled instead (CR 702.84a). A token ceases to exist. A
  creature DIES: leave and dies triggers, persist / undying, "whenever a creature you control dies"
  watchers, graveyard-entry triggers. A noncreature LEAVES: its leave triggers (an Oblivion Ring returns its
  card), graveyard-entry triggers. Orphaned Auras go as a state-based action.
* **exile**: one permanent at a time through `ResolveCastTriggerTargets` (exile mode): the card goes to exile
  (a token ceases to exist), attachments fall off, leave triggers fire (no dies triggers), the type statics
  refresh, orphaned Auras go.
* **exile_until**: as `exile`; each exiled CARD is scheduled to return at the start of round `ret` (before the
  goldfisher's untap step, before that round's new effects). It returns through the shared battlefield put
  (`PutCardOntoBattlefield`): a NEW object (CR 400.7) under its owner, untapped, no counters, with its enter
  triggers firing again; a land fires landfall. It enters before the goldfisher's untap step, so that untap
  clears its entered-this-turn flag: under CR 302.6 it CAN attack in round `ret` (it has been under its
  controller's control since the goldfisher's turn began), exactly as a card returned during the control
  player's upkeep can. A token exiled this way never returns. If the card left exile some other way in
  between, the return is logged `applied: 0`, outcome `no longer in exile`. For Parallax Wave with `j`
  exiles on round 3 and fading 5, `ret = 9 - j` (design doc section 4.3a). Opalescence's self-exile loop
  (exiles for good) is `exile` instead.
* **bounce**: `ReturnPermanentToOwnersHand` (a token ceases to exist; an unearthed permanent is exiled
  instead; leave triggers; revolt armed), then orphaned Auras go.
* **tap**: the chosen untapped permanents become tapped after the untap step. No trigger; at most once per
  round per effect. In a skipped round it is logged as a no-op (`skipped: true`): there is no untap step.
* **discard**: the shared discard hook `DiscardCardFromHand` (madness exiles and offers its cast,
  resolution-time default; Curator of Mysteries' scry; a "shuffle into library instead" replacement), one card
  at a time in candidate order. A staged card (Light Up the Stage's exile) is not in the hand and is never a
  candidate. Hand cards are typed through the card database (a placeholder hand card has its real types).
* **life**: `OpponentGainsLife(goldfisher, amount)`: the passive opponent gains life the way every other
  opponent gain does, so the goldfisher's Tainted Remedy turns it into a loss, Screaming Nemesis's lockout or
  a "players can't gain life" static stops it. The gauntlet deck's search sees the new total from that
  round on, which is the USER's "force the engine to hit a higher life total" replay (design 5.6). The log
  carries `delta`, the life actually gained.
* **tax**: while round t is in `[R, R2]`, every goldfisher spell pays `{amount}` more generic (`all`), or
  only its noncreature (Thalia) / creature spells. Taxes add up. Priced in `EffectiveSpellCost` exactly where
  Thalia's tax is (three sites), so the goldfisher's search plans with it and the executor pays it. Known
  gap, shared with Thalia here: a spell cast without paying its mana cost (cascade, a free cast) is not
  taxed (CR 118.9d says it should be). Logged twice: `phase: "start"` at R, `phase: "end"` at R2 + 1. Limits:
  `all` <= 255, a class tax <= 15 in total.
* **skip**: the goldfisher's turn of round t does not happen: no untap, upkeep, draw, main, combat, end or
  cleanup step. The round still advances (so a game that was going to win on round t or later wins exactly
  one round later, the analytic check), and the passive opponent's side of the round still happens: its
  creatures scheduled for round t arrive (`MaterializeOpponentSpawns`, the untap step's site; without it
  Burn's Searing Blaze lost its creature target and the prototype's skip measured +2 in some games) and it
  takes its own turn of round t (its draw, its land, the free-turn lock check). Forbidden Orchard's Spirits do
  not appear (no Orchard was tapped). It is an opponent's EXTRA turn, not the goldfisher's: the
  goldfisher's turn count (`GameState::TurnSeq()`) does NOT advance (`ctrl_skipped_turns` is subtracted), so
  every "until the end of your next turn" / "this turn" / next-upkeep window stays open through the skipped
  round (Light Up the Stage's staged cards; the prototype lost them, design 8.3). Windows audited: staged /
  impulse expiries (`StagedCard::expiry_turn`, `m_staged_expiry`; all TurnSeq-based), exert ("doesn't untap
  during your next untap step": no untap step is consumed), suspend / fading / vanishing / Pact upkeeps (no
  upkeep happens), top-of-library intent (`top_stacked_turn`, TurnSeq-based), the same-turn stamps
  (m1_hand_turn and the rest: TurnSeq-based). Every one of them is keyed on TurnSeq or on a step that a
  skipped turn does not run.
* **counters**: the candidates are the goldfisher's SPELLS (never abilities, never the opponent's) that reach
  resolution through the executor's stack in that round, in resolution order; index i = the i-th of them
  (0-based), counting spells already countered. One counter effect takes at most one spell per round (`all`:
  every spell). Several counter effects in one round are tried in spec order; a spell is countered at most
  once. A spell that matches is moved and never resolves; its mana stays spent, and its cast triggers
  (storm, prowess, cascade) have already happened, as the rules have it (CR 603.2). Destination:
  * a copy ceases to exist (CR 707.10a);
  * a flashback spell is exiled whichever counter it is (CR 702.34a);
  * `counter`: its owner's graveyard (CR 701.6a) with graveyard-entry triggers, or the "shuffle into its
    library instead" replacement;
  * `counter_to_hand`: its owner's hand (`ReturnCounteredSpellToHand`, the engine's own Remand route);
  * `counter_to_top`: the top of its owner's library, as a KNOWN card (the owner saw it go there).
  The card moved is the PRINTED card: a "(Kicked)" face's base (CR 608.2m), an adventure spell's whole card
  (CR 715.4), an MDFC / face-down spell revealed (CR 708.9), unstaged. Mana value on the stack = the printed
  card's (kicker is not part of it, CR 202.3) plus X (CR 202.3e).
  "Can't be countered" (`cant_be_countered`, CR 101.2): an INDEX counter aimed at it is spent and does
  nothing (`applied: 0`, outcome `can't be countered`); an `mv>=` / `name=` / `all` counter skips it and
  keeps waiting. Split second is not modelled (no card in the pool has it).
  A counter that found no spell in its round is logged `applied: 0` at the start of the next round (or at
  the end of the game), with that round's spells as its candidate list.
  Not modelled: Remand's own draw for the CONTROL player (it lives in the control deck's game, design 5.7);
  a spell resolved outside the executor's stack (a Remand-rider cast's target, a cascade's free spell that
  resolves inside its parent's resolution) is not a candidate.

**proven_no_win.** The goldfisher's search may prove that no line wins through `max_turns` and end the game
early. While a benefit it cannot see is still due (an `exile_until` return, a tax with rounds left), the
executor ignores that proof and plays on.

## 6. Outputs

### 6.1 `[ctrl-inject]`: one line per effect application (whenever `ctrl_inject` is set)

    [ctrl-inject] {"job":"burn__ctr3","gi":4,"round":3,"effect":"counter:3:mv>=1","n":0,"kind":"counter",
                   "features":{...},"count":2,"candidates":["Lightning Bolt","Light Up the Stage"],
                   "what":["Light Up the Stage"],"mv":3,"applied":1,"outcome":["graveyard"]}

| key | meaning |
|---|---|
| `job`, `gi` | the job name and the GLOBAL game index (`game_index + k`) |
| `round` | the round it applied in |
| `effect`, `n` | the effect as written and its 0-based position in the spec |
| `kind` | the canonical kind name |
| `count` | the number of candidates (for a counter: spells reaching resolution this round so far, this one included; at a no-op: all of that round's spells) |
| `candidates` | their card names, in candidate order (index i = position i) |
| `what` | the names actually chosen |
| `applied` | how many were affected (left the battlefield, were tapped / discarded / countered; 1 for a skip, a tax start, a life gain that changed the total). `0` = a no-op |
| `outcome` | per chosen object: board `destroyed` / `indestructible` / `umbra armor: ...` / `gone`, or `left` / `stayed` / `token: ceases to exist`; counters `graveyard` / `hand` / `library top` / `exile (flashback)` / `copy: ceases to exist` / `can't be countered`; returns `returned` / `no longer in exile` |
| `phase` | `tax`: `start` / `end`; `exile_until`: `return` on the return line |
| `ret` | `exile_until`: the return round |
| `amount`, `delta`, `class`, `mv`, `skipped` | kind-specific (life asked / gained, the tax class, the countered spell's mana value, a no-op in a skipped round) |
| `features` | the goldfisher's state as the effect meets it (below) |

**`features`** (the opponent-state features of design section 7 stage 1, for the learned model of design E;
"the goldfisher" is the gauntlet deck being disrupted): `lands_bf` (lands it controls), `untapped_lands`,
`lands_hand`, `creatures` (creatures it controls), `nonland_perms`, `nonland_hand` (nonland cards in hand),
`hand` (hand size, staged cards excluded), `staged` (playable exiled cards), `library`,
`spells_this_round` (its spells that reached resolution this round so far; at a counter, the spell being
countered included), `spells_last_round` (its spell
count in the previous round; 0 after a skipped round), `opp_life` (the control deck's life). Board effects
are measured BEFORE the untap step, so `untapped_lands` is what the previous turn left untapped.

### 6.2 `[ctrl-game]`: one line per game (only with `ctrl_record: true`)

    [ctrl-game] {"job":"burn__base","gi":4,"wt":4,"kill":"life","through_life":true,"start_life":20,
                 "life_gain":0,"skipped":0,"free_turns":0,"rounds":[{"r":1,"life":17,"dmg":3,"life_gain":0,
                 "poison":0,"opp_library":46,"spells_cast":1,"spells":["Lightning Bolt"]}, ...]}

| key | meaning |
|---|---|
| `wt` | the game's result as the batch scores it (`-1` unwon, `-2` voided) |
| `kill` | how the passive opponent lost: `life`, `poison`, `decked`, `alt` (an "you win the game" effect, Maze's End), `other`, or `none` (unwon). `through_life` = `kill == "life"`: only those kills can be delayed by life gain (design 5.6) |
| `start_life` | the control deck's life at the start (20; 30 is 2HG, out of scope) |
| `life_gain` | the net life the `life` effects added over the game |
| `skipped`, `free_turns` | rounds skipped by `skip`; the engine's free turns (MTG_FREE_TURNS) |
| `rounds[]` | one entry per round reached, the state at the END of the round (after the opponent's turn; for the last round, the state when the game ended) |
| `rounds[].dmg` | the DAMAGE CURVE: life the goldfisher has taken from the control deck by then, net of the `life` effects so far: `start_life + life_gain(so far) - life`. Poison and decking are separate columns |
| `rounds[].spells` | its spells that reached resolution that round (the counter candidates of that round) |
| `rounds[].spells_cast` | `GameState::spells_cast_this_turn` at the end of its turn (every cast, including ones resolved outside the stack loop) |

The baseline table of design section 7.5 is a `ctrl_record: true` job with NO `ctrl_inject`, at
`max_turns: 15`, on the fixed held-out seeds. It is byte-identical to the plain job (recording changes no
play) and gives each game's kill turn, its kill kind and its damage curve.

## 7. Driver recipes

* **Single-effect delay table** `S[e, t, g]`: one job per (gauntlet deck, effect, round), all sharing the
  deck's seed and `game_index` with the base job; delay per game = `wt(arm) - wt(base)`.
* **Clairvoyant target** (Q3 / Q8), a board / hand effect on round T: run the index-0 arm first; its
  `[ctrl-inject]` line per game gives `count` and `candidates` at that moment (the game is identical to the
  base game up to round T, so the count is the base game's). Then run one arm per index `i = 1 ..
  max(count) - 1`. Per game g, the clairvoyant delay is the largest delay over the arms with `i <
  count(g)`; an arm with `i >= count(g)` is a no-op (`applied: 0`) and equals the base game. With a PRIOR
  effect in the schedule, the counts must come from that schedule's index-0 arm, not from the base.
* **Clairvoyant counter** on round T: the base job with `ctrl_record` lists, per game, the spells that
  reached resolution in round T (the `rounds` entry with `"r": T`, its `spells`). Arm `counter:T:i` counters
  the i-th of them; the game is identical to the base game up to that spell. Sweep `i = 0 .. len - 1`.
* **Life-gain screening** (design 5.6): from the base job's `[ctrl-game]` curve, a gain of L on round t is
  provably delay 0 when `dmg` at the kill round is >= `start_life + L` (the recorded line still wins on that
  round); otherwise run `life:t:L` to measure it. A `kill` other than `life` gets no delay from life gain.
* **Analytic skip check**: `skip:T` must give `wt(arm) = wt(base) + 1` for every game with `wt(base) >= T`, and
  `wt(arm) = wt(base)` for every game won before T (section 9 has the measured result).

## 8. Simplifications and limits (state them in any report built on this)

* The control player is always on the play; an instant on the goldfisher's turn is modelled only by the
  counters and `tap`. No blocking (design 5.6, out of scope).
* Board effects are aimed by fixed selectors; there is no in-game target heuristic beyond `big`. The searched
  / clairvoyant choice is a driver sweep over indices.
* 2HG is out of scope: the effects and the record read the first opposing head only.
* A counter reaches only spells that resolve through the executor's stack loop (`GameEngine::ResolveStack`).
* The tax misses free casts (as Thalia does here).
* Ward is taken as paid; protection from a colour is ignored (the effect is colourless).

## 9. Validation (2026-11-27)

All on the build of commit `84e204640` plus the spawn fix (section 10), Release, run in the small-job lane.

* **Unit tests** (`test/unit/test_control_inject.cpp`, 9 cases): the grammar, aliases and refusals; destroy
  through the dies cascade (Kitchen Finks persists back with a -1/-1 counter and its ETB fires; Ulamog is
  indestructible; a hexproof Slippery Bogle is no target but dies to `all`); umbra armor; exile_until return
  as a new untapped object, a token never returning; bounce, tap (once per round), discard by `name=` and
  `big`, life; tax by class through `EffectiveSpellCost`, summed and ended; counters by index / `mv>=` /
  `name=` into graveyard / hand / known library top; abilities and the opponent's spells are never
  candidates; flashback exiled, a copy gone, "can't be countered" (index spent, policy waits); a real
  `skip` turn through `GameEngine` (round +1, TurnSeq unchanged, no untap, no draw, the opponent's spawn
  arrives and its draw happens, a staged card still live next turn) against an unarmed control turn.
  The whole suite: 4342 / 4342 cases pass, on the final build.
* **Hook off = byte-identical.** `regression.sh --smoke --deck=burn,whiteknights,soldiers` (THREADS=2,
  MTG_PGO=0): 9 / 9 PASS against `test/regression_gt.txt` (`burn_smoke_d3_s1001 = 4.4400/66921a2e4aaff4ad`,
  `whiteknights_smoke_d3_s1001 = 4.1467/13fe195aa47a42c7`, the d0 / d5 cases, and Soldiers' three: the
  Thalia tax site), per-game audit play-changed 0. Run before and after the spawn refactor.
* **`ctrl_record` changes no play**: Burn and White Knights 200 games, base vs base + `ctrl_record`: the
  same digest (`04f54691da8c89e1`, `31314660b59aa8fa`).
* **Analytic `skip:3`** (200 paired games each, seed 7900001, d3 b10, `max_turns` 12, the probe's shape):
  mean delay Burn **+1.000** (prototype +1.12; seed 7900005 / gi 4 now 4 -> 5), White Knights +1.000,
  Stompy +1.000, Breaching +1.000, Dragons +0.995. 999 / 1000 games are exactly +1. The exception, Dragons
  gi 1 (5 -> 5), is the budgeted search finding a different line after the skip (base casts Dragon Tempest on
  round 4 and leaves the opponent at 1; the skip arm attacks for 7 and then 9), not a rules difference.
  `skip:3-5` at `max_turns` 15 on Burn (100 games): 98 exactly +3; gi 12 and 62 are +2 because the passive
  opponent loses life on its own turns in those games (gi 62: 12 -> 11 -> 10 over the skipped rounds), so the
  goldfisher needs one turn fewer. That is correct: the opponent's turns keep happening.
* **Every kind ran in a real batch** (Burn and White Knights, 40 games each, with `ctrl_record`):
  `exile_until:3:creature:all:ret=5`, `tap:2-4:land:0`, `tax:2-4:1`, `life:3:5`, `counter_to_hand:3:mv>=1`,
  `counter_to_top:3:mv>=1`, `skip:3-4`, `discard:2:name=Lightning Bolt`, `bounce:3:creature:big`,
  `counter:3:0`, `counter:3:1`; their logs were read (returns at round 5 one line per card, tax start / end,
  life delta, the index-1 counter's no-op with the round's one spell as its candidate list).
* **`max_turns` 15**: Burn 100 games at 15 with and without `skip:3-5` (latest win round 11), no error.

## 10. Checkpoint

**Done** (branch `feature/control-mode`, local, not pushed):
* `84e204640` the feature: `src/core/ControlInject.{h,cpp}`, the hooks, the job fields, the tax, the
  `GameState` bytes, unit tests, the probe script on the new grammar.
* `c833f5e22` spawn arrival in a skipped round, the spawn unit check, this document.

**Remaining (not stage-1 engine work):** the gauntlet baseline table (a `ctrl_record` job per gauntlet deck at
`max_turns` 15 on fixed held-out seeds) and the delay-table / clairvoyant-sweep drivers (section 7); blocking
(design 5.6); a Remand / Memory Lapse control-player draw lives in the control deck's game (design 5.7).

**Commands**
* build: `./build.sh` (from the worktree); unit: `build/Release/mtg-test -tc='ctrl_inject*'`
* byte identity: `env -C <worktree> bash scripts/box_lane.sh --slots 2 -- env THREADS=2 MTG_PGO=0 bash
  test/regression.sh --smoke --deck=burn,whiteknights,soldiers`
* a probe: `python3 scripts/control_inject_probe.py make <manifest> --games 200 --seed 7900001`, then
  `scripts/box_lane.sh --slots 2 -- build/Release/mtg --batch <manifest> --threads 2 --game-log-dir <dir>
  2> <dir>/batch.err`, then `python3 scripts/control_inject_probe.py report <dir> <dir>/batch.err`.
* reproduce one game: `"seed": BASE + gi, "game_index": gi, "games": 1` (both, or the game differs), plus
  `--game-trace-dir <dir>` for its turn-by-turn trace.

### Drivers (stage 1, 2026-11-27): `scripts/control_gauntlet.py`

Tests: `python3 test/control_gauntlet_test.py` (parsers, baseline table, planner, delay / clairvoyant / interaction
math, skip check, report end to end; synthetic data, no engine).

* **Gauntlet** = every `test/regression_cases.sh` deck whose modes marker is not `goldfish: false` (65 on
  2026-11-27), each at its SHIPPED play setting: the regression tier's depth-5 row, emitted like the harness's d5
  rows (no `depth` key, so the profile's `value_play` block picks the depth; `budget_ms` = the row's budget).
* **Seeds:** `seed = 8100001 + gi`, `game_index = gi` (stage-2 report section 4: held out from every tier and from
  the probe's 7900001). A deck's seed set is a prefix `gi = 0..n-1`, so a table at n = 20 extends to 40 (the
  stage-2 parity split) by adding `gi = 20..39` only.
* `baseline make <dir> [--games 20] [--games-for k=N,..] [--max-wall-hours H --threads T]`: ONE manifest,
  `<key>__base` per deck (`ctrl_record`, `max_turns 15`, LPT weight = projected cost) plus `<key>__plain` twins
  for the `--identity` decks. The base job carries the CENSUS, five index-999 no-op effects every round
  (`destroy:1-15:land:999;destroy:1-15:creature:999;discard:1-15:card:999;tap:1-15:land:999;tap:1-15:creature:999`):
  a no-op changes nothing (`applied: 0`, no re-plan) and logs each round's candidate lists, which the clairvoyant
  sweep needs to size itself in the same pooled batch. **Verified inert:** 5 decks x 60 games, base (census +
  record) vs plain, 300 / 300 per-game digests identical.
* `baseline ingest <dir> [--out logs/control-mode/baseline]` -> `<key>.json` (`ctrl-baseline-v1`: per game `k`
  (null = unwon by 15), `wt`, `digest`, `otp`, `kill`, `alt`, `dmg[]` per round, `spells[]` per round, the census
  `cands{}` and `features[]` per round) and `summary.txt`. Stamp = invalidation keys: `decklist_sha256`, every
  play-read sibling artifact's sha256 (profile, value, keep table, buckets, ...; `.raw.json.gz` excluded),
  `src_tree` (+ `src_dirty`), `harness`, `seed_base`, `n`, `max_turns`, the depth actually played (`[play]` line),
  `budget_ms`, `play_digest` (the job digest). `baseline check` lists stale decks; `table make` refuses a stale one.
* `plan --what baseline|table [--games N] [--threads T] [--baseline-dir D]`: core-hours. Per-game cost = the
  baseline's measured cpu when it exists, else `test/suite_cost.json` (worst shape, upper bound; `--estimate`
  scales it by `test/job_cost.tsv` units). The table's game count is exact from the baseline (screening and
  sweep sizes included); disrupted games are priced at 2.5x a baseline game (`--arm-factor`; measured on the
  pilot, cost-weighted: Stompy 2.9, Burn 1.9, Dragons 1.6, White Knights 1.2, Breaching 1.0).
* `table make <dir> --baseline-dir D [--decks ..] [--games N] [--rounds 2-5] [--life-screen]`: per deck and round
  t: fixed-policy singles `land` (destroy land 0), `crt` (destroy creature big), `sweep`, `ctr` / `ctrhand` /
  `ctrtop` (the first spell of mana value >= 1, the probe's policy = the Q8 threshold at zero), `disc` (nonland
  big), `tax` (`tax:t-15:1`), `tapland` (land 0), `tapcrt` (creature big), `life3`, `life5`, `xu`
  (`exile_until` creature big, `ret = t + 2`), `xuall` (all creatures, `ret = t + 2`), `skip`; the CLAIRVOYANT
  sweep (Q3 / Q8) for land, crt, the three counters, disc, tapland, tapcrt: one job per candidate index, run only
  on the games that have that index (hand cards deduped by name; counter candidates from the base round's
  spells); and 20 joint stacking arms (section 8's plus mixed pairs). Screening: a game won before round t is not
  run (delay 0 exactly); `--life-screen` also zeroes the section 5.6 provable-zero life games. One job per arm,
  `skip_games` for the gaps, `seed = BASE + first gi`.
* `report <dir> [--out f] [--json f] [--rows f]`: S[e,t,g] fixed and clairvoyant with se, hit rate, unwon at 15,
  the interaction and increment terms (8.2), the analytic skip check, life gain split by deck class (creature
  share of nonland cards: < 0.40 burn, >= 0.55 creature; combo when most kills are not through life) with the
  race-flip share P(K < w <= K + D) at w = 4..8, the life-screen validation, and `--rows` = one JSON row per
  (deck, arm, game) with the delay and the `features` at application (the design-E training rows).
* Delay convention: `delay = K' - K`, unwon arm = 16 (`control_score.delay(..., censored="bound")`), an unwon base
  game = 0.

**Runs (2026-11-27, small-job lane, 2 threads; heartbeat 2/2 busy):**
* Baseline `build/scratch/control-mode/baseline1` -> `logs/control-mode/baseline/<key>.json` + `summary.txt`:
  65 decks x 20 games (the five probe decks 60), 1,800 games, 0.26 core-hours, 8 min wall, NOTHING dropped
  (`plan` projected 1.1 core-hours from suite_cost). Equal-weight mean kill round 4.574; no game unwon at 15, none
  past 12 (latest kill round 11, Beasts). Non-life kills: Maze's End alt 20/20, Mill decked 20/20, Accomplished
  Alchemist decked 12/20, Eldrazi Displacer Flicker decked 2/20.
* Pilot `build/scratch/control-mode/pilot1`: five probe decks x 60 games, rounds 2-5, every kind, clairvoyant +
  joint, no life screen. 4,564 jobs, 51,987 games, 2.56 core-hours, 78 min wall (`plan` said 0.65 h at the old
  1.3x arm factor; hence 2.5x). Report: `logs/control-mode/pilot-report.txt` (+ `.json`, `pilot-rows.jsonl` with
  the state features). Against section 8 (d3 b10, max_turns 12, 200 games; the pilot is at PLAY settings and
  horizon 15, 60 games), mean of the five decks: land 2/3/4 1.05 / 0.82 / 0.35 (probe 1.06 / 0.84 / 0.31); crt
  0.50 / 0.63 / 0.53 (0.52 / 0.58 / 0.53); sweep3/4 1.27 / 1.71 (1.26 / 1.76); counter 0.57 / 0.82 / 0.55 (0.54 /
  0.82 / 0.52); discard 0.40 / 0.37 / 0.22 (0.51 / 0.48 / 0.24). Interactions: land (3+4) +0.37 (probe +0.45),
  crt +0.13 (+0.13), counter +0.07 (+0.03), two counters in round 3 -0.39 (-0.38), sweep3 - crt3 +0.64 (+0.68).
  Clairvoyant vs fixed: discard 1.16 vs 0.40 on round 2 (the largest gap), counter 0.95 vs 0.82 on round 3
  (Stompy 1.52 vs 0.95), land and creature within 0.05. Skip check: 923 / 925 exact; the two are Burn gi 12
  (base 4, skip2 and skip3 also 4: the passive opponent's own life loss, as the section 9 skip:3-5 cases).
  Life-screen validation: of 897 games the 5.6 screen would zero, 895 measured 0; Stompy gi 29 measured +1
  at life3 rounds 2 and 3 (the budgeted re-search found a slower line), so the screen is NOT exact and stays
  off by default. Life gain (USER 5.6): Burn life3 +0.45-0.55, life5 +0.65-0.72 per round 2-4; the creature decks
  +0.13-0.18 / +0.19-0.27; race-flip share at w = 5: Burn 0.25-0.38, creature decks 0.11-0.26.

**Projected full table** (`logs/control-mode/plan-full-table-20.txt`, arm factor 2.5): 65 decks x 20 games,
rounds 2-5, every kind, clairvoyant + joint = 206,590 games, **107 core-hours = 4.4 h at 24 threads**; without
the clairvoyant sweeps 47 core-hours; about 2x either at 40 games (the stage-2 parity split). The largest shares:
Worldfire 23.0 core-hours (32,240 games: its counter sweeps run over many spells a round), Beasts 14.0, Hinata
8.6, Snow 6.3, Gods 6.1, Maze's End 4.9. Launch: `table make <dir> --baseline-dir logs/control-mode/baseline
--games 20`, then one `mtg --batch` under the box lock (a big run, not the lane).
