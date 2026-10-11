# Brief: implement a USER-settled cleanup-discard design (2026-10-09)

You are implementing ONE OR MORE decks' cleanup-discard policy in the MagicDeckTester C++ engine. You
work in your OWN git worktree (your current working directory; run `git rev-parse --show-toplevel`
first and use that absolute path everywhere). Other agents are implementing other decks in parallel
in their own worktrees; the integrator cherry-picks your commit afterwards.

## Source of truth
- `docs/design/<deck>-discard-policy-proposal.md` (or `fungus-cleanup-discard.md` section 6,
  `selesnyalifegain-discard-policy.md`, `whiteknights-discard-policy.md`). The top section
  "USER-revised design (2026-10-09) -- SUPERSEDES the proposal below" is the review record; its
  `#### <deck>: SETTLED` block is THE design. USER rulings are quoted. The original proposal below it
  is background (card census, param names, rationale, test boards) and is superseded wherever it
  differs from the USER section.
- These are USER-OWNED decisions (CLAUDE.md: "A USER-OWNED DECISION IS NEVER SETTLED BY AN AGENT").
  Implement the USER's design faithfully. Where it is ambiguous or not expressible, take the
  documented agent default or the most literal reading, mark it in a code comment as
  "agent reading", and LIST it in your final report. Do not silently simplify a rule away.

## Read first
- `CLAUDE.md` (repo root) -- especially: heuristics live only in DecisionProviders; EnvOn flags;
  never wrap commands in a timeout; no `cd X && ...` in Bash (use absolute paths / `git -C`); never
  `rm` on variable paths.
- `.claude/skills/coding-conventions.md` (flags).
- At least one existing override end to end, as the pattern: `SoldiersProvider::CleanupDiscardCandidates`
  (search the name in src/ai/DecisionProviders.cpp; compact, two buckets + Vial), and
  `PreventDamageProvider::CleanupDiscardCandidates` (a keep LADDER built with `take()` then emitted
  as shed order) or `MinotaurProvider::CleanupDiscardCandidates` (distance-to-castable / EV
  discounting). Reuse shared helpers you find rather than copying them; if you need a helper another
  provider already has as a file-static, you may move/generalise it only if trivially safe --
  otherwise write a local lambda.
- The protection rules: `CleanupDiscardProtected` and `CleanupDiscardRankingWithOrder` in
  src/core/SpellEffects.h. The deck profile's `mulligan.required_pieces` / `discard_protect` are
  engine-enforced AHEAD of your order (a protected card is vetoed from the preference tier). The new
  hook `DecisionProvider::CleanupDiscardProtectionWaived(state, card)` (src/ai/DecisionProvider.h)
  lets a provider waive that protection for the discard only (required_pieces also gates the
  mulligan, so never edit the profile to unprotect a card).

## The implementation contract
- Override `CleanupDiscardCandidates(const GameState&, const std::vector<std::string>* required_pieces) const`
  on the deck's provider (declare it in src/ai/DecisionProviders.h, define it in
  src/ai/DecisionProviders.cpp next to that provider's other methods). First confirm which provider
  the deck actually RESOLVES to (ResolveProvider / the profile's archetype routing) -- e.g.
  slivers_vial routes to VialProvider, Knights to KnightsProvider (derives from VialProvider),
  WhiteKnights to WhiteKnightsProvider (derives from KnightsProvider).
- Gate it exactly like this (the heurarm slot ALREADY EXISTS in src/ai/HeuristicArm.h; the slot name
  is given in your deck brief):
  ```
  static bool XBucketDiscardEnabled()
  {
      static const bool on = EnvOn("MTG_X_BUCKET_DISCARD", true);   // DEFAULT ON; =0 -> generic
      return heurarm::Flag(heurarm::X_BUCKET_DISCARD, on);
  }
  ```
  and `if (!XBucketDiscardEnabled()) { return GenericProvider::CleanupDiscardCandidates(s, required_pieces); }`
  (unless your deck brief names a different =0 fallback).
- Return the FULL shed order, most expendable first, naming EVERY non-staged hand index exactly once
  ("omission = keep" is a trap: anything you omit falls to tier B, descending MV), via
  `return CleanupDiscardRankingWithOrder(s, required_pieces, shed);`. Staged cards (`m_is_staged`) are
  skipped. The natural construction is the design's KEEP ladder: take cards in keep order, then emit
  the shed order = (the explicit early-shed classes, in the design's SHED order) + the keep ladder
  read BACKWARDS. Make sure the explicit SHED order and the backwards ladder agree with the design's
  SHED list.
- Quotas count the BOARD ("net of board": a land in play fills a land slot). Duplicate legends (a
  copy on our battlefield OR a 2nd copy in hand) are dead and shed early unless the design exempts
  the card. Within a bucket, distance to castable orders the cards where the design says so.
- Hand cards are NAME-ONLY placeholders in real play: always resolve
  `CardDatabase::Instance().LookupCached(card)` for types / mana cost / params. Battlefield
  `Permanent`s carry real card data. Key rules on CARD PARAMS where a param distinguishes the role
  (e.g. `upkeep_adds_charge` = Aether Vial, `is_aura`, `produces`, `enters_tapped`); use names only
  where no param does, following what neighbouring code does for the same deck.
- Verify every card you reason about in `src/cards/data/cards.json` (mana cost, P/T, params,
  oracle_text) and the deck's decklist under `decks/<Deck>/`. Do not recall cards from memory.
- It runs INSIDE ROLLOUTS thousands of times per game: deterministic (stable sorts, ties by hand
  index), no RNG, no heavy allocation, no GameState copies. A bounded exhaustive subset search over
  the hand (<= 2^9) is fine where the design needs one.
- Comment style: match the existing overrides -- a block comment above the function citing the doc,
  the USER quotes that drive non-obvious rules, and each agent reading.

## Tests
- Write `test/unit/test_<slug>_discard.cpp` (doctest), modelled on
  `test/unit/test_prevent_damage_discard.cpp` (placeholder hand cards via a `Ph()` helper, real
  board Permanents, `heurarm::t_arm[heurarm::X_BUCKET_DISCARD] = 0/1/-1` to pin arms -- reset to -1
  at the end of each case). Cover each clause of the SETTLED design with a board whose expected
  index 0 (and, where the design pins it, the full order) you derive BY HAND from the design text,
  not from your code's output. Include: the =0 hatch returns exactly the generic ranking; every
  non-staged index named exactly once; at least one board where the generic ranking's index 0
  differs (the control arm must differ).
- Register it: add the file to the `add_executable(mtg-test ...)` list in CMakeLists.txt (append
  after the last `test/unit/...` line).
- BUILD / RUN ONLY WITH (single niced compile pinned to 4 cores, ~40 s; links against the primary
  checkout's prebuilt engine library):
  `bash /workspaces/MagicDeckTester/logs/discard_impl/minitest.sh <your-worktree-abs-path> test/unit/test_<slug>_discard.cpp`
  and `... minitest.sh <your-worktree-abs-path> --syntax` for a 5-second parse check.
  NEVER run ./build.sh, cmake, ninja, the regression harness, or any `mtg` game run: the box is
  shared with a TIMED generation run on another container, and contention corrupts it.
  The helper relies on: no new DATA MEMBERS in any class, no new virtuals on DecisionProvider, and
  edits to headers limited to src/ai/DecisionProviders.h (plus comment-only edits elsewhere).

## Scope of edits
- src/ai/DecisionProviders.h / .cpp, your new test file, one line in CMakeLists.txt, and the deck's
  design doc (you MAY append an "#### Implementation notes (2026-10-09)" subsection at the end of the
  USER-revised section listing what you implemented and every agent reading).
- Do NOT edit profiles, cards.json, decklists, references/, ground truth, or other decks' code. If
  the design needs one of those, stop short of it and report it.
- Housekeeping comment fixes named in your deck's doc are in scope when they are comments in
  src/ai/DecisionProviders.{h,cpp}; list cards.json ones in the report instead.

## Finish
- Commit on your worktree's branch: one commit per deck, message
  `feat(<deck>): the USER's cleanup-discard buckets (MTG_<X>_BUCKET_DISCARD, default ON)` with a body
  that summarises the rule, the agent readings, and the test count, ending with the line
  `Co-Authored-By: Claude Opus 5.5 (1M context) <noreply@anthropic.com>`. Do not push, merge or
  rebase.
- Final report (your last message is all the integrator sees): worktree path, branch, commit
  hash(es); per deck: the rule as implemented (keep ladder + shed order, compact); every agent
  reading / ambiguity / clause you could not express and why; anything that needs a profile,
  cards.json or engine change; housekeeping found; tests (cases / assertions, all passing).
