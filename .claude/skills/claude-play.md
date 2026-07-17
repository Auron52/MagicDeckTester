# Claude-Play Runner Skill

Authoritative guide for the **claude-play oracle** — an opt-in mode where a Claude
agent (not the encoded AI) drives a deck's main-phase decisions in the simulator.
Its purpose is **verification**: surface engine bugs (illegal/impossible plans,
missing legal plays, wrong state transitions) and AI-misplay candidates (games Claude
wins earlier than the search), feeding the analyze-deck convergence loop. Read this
before running the claude-player, building a player agent, or running a play sweep.

It assumes rules correctness per `.claude/skills/mtg-rules.md` and builds on the
engine in `.claude/skills/mtg-ai.md`. It is a verification companion to
`.claude/skills/analyze-deck.md` (Stage 5).

---

## RULE 0 — read cards.json before reasoning about any card

**Claude's recall of Magic cards is unreliable. Do NOT play or judge a card from
memory.** Before deciding (and before flagging anything as a bug), read the card's
real definition from the simulator's card database:

```
src/cards/data/cards.json
```

Confirm each relevant card's **mana cost**, **power/toughness**, **types**, **oracle
text**, and **parameters** (e.g. `on_cast_trigger_*`, `cascade_max_mv`, `retrace`,
`discard_land_damage`, `upkeep_adds_charge`).

**Scope the read to the deck you are playing — do NOT load the whole database.** The
decklist (`<deck>.txt`) bounds the relevant cards to a small set; read just those
entries once, up front, and keep them as your reference for the game:

```bash
# Print the cards.json defs for exactly the cards in this deck (counts stripped):
python3 - "<deck>.txt" <<'PY'
import json,sys,re
names=set()
for ln in open(sys.argv[1]):
    ln=ln.strip()
    if not ln or ln.lower() in ('sideboard','mainboard'): continue
    names.add(re.sub(r'^\s*\d+x?\s+','',ln).strip())
d=json.load(open('src/cards/data/cards.json'))
cards=d if isinstance(d,list) else list(d.get('cards',d.values()))
for c in cards:
    if c.get('name') in names:
        print(c['name'],'|',c.get('mana_cost'),'| P/T',c.get('power'),'/',c.get('toughness'),
              '|',c.get('oracle_text','').replace(chr(10),' '),'| params',c.get('parameters',{}))
PY
```

(For a one-off check of a single card, grep cards.json for that name instead.) This is
not optional. The two false positives from the first sweep were both card-data
errors: the player flagged a "missing two-sliver play" without knowing Muscle Sliver
({1}{G}) and Leeching Sliver ({1}{B}) are **2 mana**, not 1; and flagged a "Vial
deploying the wrong mana value" without knowing the deck's Vial is capped at 2 counters.
Reading cards.json would have prevented both.

**cards.json vs Scryfall — Scryfall is a TIEBREAKER, not a routine step.** cards.json
is what the simulator plays from, so it is the source of truth for judging ENGINE
behavior (does the engine do what its own card data says?). Reading cards.json is your
default and covers the common case. Whether cards.json faithfully reproduces the real
card is a *separate* question (the analyzer's job — see analyze-deck).

Consult Scryfall ONLY for a DATA-fidelity question — one of:
  (a) the engine faithfully matches cards.json, yet the result still looks wrong, or
  (b) a card's modeled cost / P/T / oracle text / params looks suspect.
Then check cards.json against reality (do NOT trust your own card memory — that
unreliability is the whole reason for Rule 0):

```bash
curl -s "https://api.scryfall.com/cards/named?exact=<URL-encoded card name>" \
 | python3 -c "import json,sys;c=json.load(sys.stdin);print(c['name'],'|',c.get('mana_cost'),'|',c.get('power'),'/',c.get('toughness'),'|',c.get('oracle_text'))"
```

(api.scryfall.com is reachable through the egress firewall.) Decision: engine ≠
cards.json ⇒ **engine bug** (fix the engine; no Scryfall needed); engine == cards.json
but ≠ Scryfall ⇒ **data bug** (fix cards.json via the analyze-deck loop); cards.json ==
Scryfall ⇒ **no bug** (your intuition was wrong).

Keep Scryfall use rare. analyze-deck ALREADY Scryfall-checks the high-risk data at
implementation time: Stage 2a fetches oracle text, and Stage 2d-bis (`audit_card_costs.py`)
audits every `mana_cost`/`cmc` and `cascade_max_mv` against Scryfall. So mana costs and
cascade are already covered — the runner's Scryfall backstop is for fields that audit
does NOT check (power/toughness, `on_cast_trigger_max_mv`/damage and other behavioral
params, oracle-derived flags), or a card that predates the audit. How much the runner
needs it scales inversely with analyzer quality; it is a backstop, removable if it never
usefully flags anything.

---

## The mode (opt-in, inert by default)

`--claude-play` is gated behind `AIEngine::SetExternalChooser`; with no chooser set the
normal autonomous engine is unchanged. It drives only the **main phases**; combat and
cleanup discards stay on the engine heuristics. Build Release first:
`cmake --build build --config Release`.

### Stateless-replay protocol

A game is fully determined by `(deck, seed, game-index, choices)`. Each invocation
replays the deterministic game applying the prior `--choices`, then prints the next
decision and exits:

```bash
./build/Release/mtg <deck>.txt --profile <deck>.profile.json \
  --claude-play --seed <S> --game-index <GI> --max-turns 8 --reveal 6 --choices "<CSV>"
```

- Start with **no** `--choices` (or empty). The command prints ONE decision between
  `<<<CLAUDE_DECISION>>>` and `<<<END_DECISION>>>` and exits **70** ("more input
  needed"). The JSON has: turn, phase, your life/battlefield/hand/graveyard,
  `library_size`, `upcoming_draws` (next up-to-N draws if `--reveal N`), opponent
  life/battlefield, and a `plans` array of LEGAL plans each with an `index` + `summary`.
- Choose exactly ONE plan index, **append** it to the CSV, re-invoke with the FULL
  accumulated CSV (e.g. ``→`"2"`→`"2,0"`→`"2,0,1"`…). Never drop earlier choices.
- `-1` passes (cast nothing). When you see `<<<CLAUDE_RESULT>>> { "win_turn": N … }`
  `<<<END_RESULT>>>` (exit 0) the game is over.
- **Each decision has a `type`.** `"main_phase"` (the default — reply a plan index as
  above). `"vial_charge"` — an Aether Vial upkeep charge decision: reply **1** to add a
  charge counter this upkeep or **0** to hold (the Vial deploys a creature whose mana
  value EQUALS its counters; the JSON gives `current_counters` and the AI's
  `heuristic_default`). `"mulligan"` — a London keep/mulligan decision (one per attempt,
  emitted FIRST, before any turn): reply **1** to keep this `hand` or **0** to mulligan
  again; `ai_choice` is what the engine's KeepHand would do. `"bottom"` — after a keep,
  put one card on the bottom (fires `mulligan_count` times): reply the 0-based `hand`
  INDEX to bottom; `ai_choice.index` is the engine's pick and each hand card carries a
  `win_optimal` flag (depth > 0). Following `ai_choice` at every mulligan/bottom step
  reproduces the autonomous search's exact opening hand. All decision types share the one
  `--choices` stream in the order they occur (mulligan/bottom first, then per turn a
  vial_charge before its main_phase). Not emitted under `--force-mulligan` (that
  reconstructs a fixed hand on the engine).
- `--reveal N` exposes the top N upcoming draws (partial clairvoyance). The search is
  fully clairvoyant; reveal gives the player a fair-ish, limited foresight.

Reproduce any game (e.g. to inspect a flag) by replaying its exact CSV.

---

## Playing well

The oracle is only trustworthy if Claude plays competently — an underplaying Claude
produces **false negatives** (the search looks optimal when Claude is just worse).
Read cards.json (Rule 0), then play to the deck's actual game plan. General principles:

- **Develop every turn**: play a land if offered; deploy threats. In goldfish the
  opponent is passive and cannot punish your life, so cards with self-damage downside
  (e.g. Eidolon of the Great Revel's own trigger) are nearly free — play the body.
- **Race**: maximize damage per turn toward the fastest kill; sequence noncreature
  spells before combat to capture prowess; use `upcoming_draws` to plan lethal.
- **Combo decks** (e.g. Treasure Hunt + Land's Edge): set up the engine before firing —
  understand each piece from cards.json before committing a line.
- A creature-target spell being **absent** when the opponent has no creature is correct,
  not a bug.

---

## Flagging bugs (the primary value)

While playing, act as a correctness sweep. A flag must be **verified against cards.json
+ the rules skill BEFORE reporting** (see Rule 0). Legitimate flags:

- a plan that is **illegal/impossible** for your actual mana + hand (check costs!),
- an obviously-legal play **missing** from the plan list (confirm it is truly legal —
  right mana, colors, additional costs),
- a state delta that is **wrong**: compute the expected life/board change from your
  chosen plan (using real card text) and compare to the next decision's state.

Merely-suboptimal plans are NOT flags. When the protocol does not expose enough state
to confirm (it currently omits some hidden permanent state, e.g. Aether Vial charge
counters), report the flag as **uncertain** rather than confirmed, and note what state
was missing.

Bugs found this way are real: the prototype caught an Eidolon on-cast self-trigger in
the rollout and a Throes-of-Chaos retrace cast that silently no-ops — both
enumeration↔execution / rollout-vs-real divergences in `ApplyPlanDirect`.

---

## Running a sweep (many games)

To sweep across games/decks, run one player per game and compare each to the search
benchmark for the SAME game:

```bash
# benchmark (the current search) for one game:
./build/Release/mtg <deck>.txt --profile <deck>.profile.json --games 1 \
  --seed <S> --game-index <GI> --depth 5 --budget-ms 200 --lookahead-bottoming
# -> "Avg win turn : N" (single game => N is the win turn; "No wins recorded." => no win)
```

A player agent: reads cards.json for its deck, benchmarks the AI, plays via the
protocol, and returns `{ai_win, claude_win, choices, flags[], summary}`. Aggregate:
`claude_win < ai_win` ⇒ AI-misplay candidate (inspect the search); any `flags` ⇒
engine-bug candidate (verify, then fix via the analyze-deck convergence loop). For a
large sweep, fan the players out with the Workflow engine (one agent per game).

This is the mechanism behind **analyze-deck Stage 5d**, the final 100-game validation
sweep: pick a base seed disjoint from the regression suite's seeds, fan game-indices
`0..99` out with the Workflow engine (one agent per game; 100 exceed the concurrency cap
and queue — expected), and feed the aggregated flags back into that skill's convergence
loop. Legality/invariant flags are the gating signal; win-turn deltas are weak. Any real
issue found gets investigated and fixed, not averaged away.

**Expectation-setting:** against the strong, clairvoyant search a guided Claude is
competitive but rarely faster (the first 30-game sweep found 0 misplay candidates).
The oracle's proven worth is **bug-finding**, not beating the AI. Treat win-turn
comparisons as a weak signal and the invariant/legality flags as the strong one.

### Recording a sweep for the `claude_sweep` gate

`verify_deck.py` (the enforcement spine) has a `claude_sweep` gate that enforces this
step is done and clean. After a sweep, verify every flag against cards.json + the rules
skill, fix the real ones via the analyze-deck loop, then record the outcome in the
per-deck ledger `docs/design/analysis-<deck>.md` under a user-owned heading (OUTSIDE the
generated `verify_deck:begin/end` block), e.g.:

```
## Claude-play sweep
- commit: `<git sha the sweep ran at>`
- seeds: <base> games: <N>
- flags: 0 unresolved      # or N; list each unresolved one as a bullet below
```

The gate reads the `flags: N unresolved` line: **0 → PASS**, **≥1 → blocking FAIL**,
**absent → disclosed SKIP** (run the sweep). A recorded commit different from HEAD is
disclosed as a staleness note (re-run if play changed — the mechanical `play_invariants`
gate + the smoke digests track whether play changed live, so a doc-only commit needn't
trigger a re-sweep). The mechanical determinism/integrity/progress half is ALWAYS
enforced by `play_invariants` (`scripts/play_invariants.py`); this judgment half is what
the record covers.

---

## Known gaps / improvements

- No persistent game log yet — add `--log-dir` (write a per-game decision trace on
  completion) so flagged games are inspectable without re-deriving the CSV.
- Decision JSON should expose **card mana costs** and **Aether Vial charge counters**
  (and similar hidden state) to cut card-data false positives.
- Aether Vial upkeep counter is auto-charged to `vial_target_mv`; modelling it as a
  decision (the optional "may" counter) is a worthwhile engine enhancement.
