# Committed-line replay: verify lockstep before replaying (deferred)

Status: DEFERRED (2026-10-09). Found while root-causing Prevent Damage s108346 gi345, a game the shipped
d5/b20 search won on T5 and every LARGER search (d5 b100/b1000, d6, d8 b100, d8 b0) never won.

## What happened

1. **The desync.** The search's apply path (`TurnSolver.cpp`, `ApplyPlanDirect` -> `apply_one`) shuffled a
   resolved Green Sun's Zenith into the library as the card DEFINITION's template (`m_number 0`); the
   executor (`EffectHandler::MoveToGraveyard`) shuffles the physical copy. `Library::ShuffleByKey` ranks
   every card by `splitmix(seed, m_number)`, so the Zenith sat at a different depth in the lookahead than
   in the real game, and the lookahead "drew" a Zenith on T3 the game never dealt. Fixed by
   `MTG_GSZ_SHUFFLE_COPY_ID` (default ON; the cast copy goes in).
2. **The amplifier -- this document's subject.** At full depth the engine replays a VERIFIED committed
   line phase by phase (`AIEngine.cpp` ~3523-3552) WITHOUT checking that the real hand still matches the
   hand the line was planned against. On gi345 the T3 Zenith cast was silently dropped (not in hand), T4
   cast Manabarbs blind with no gain engine (15 -> 10 life), T5 dropped Vito, and only at T6 did a fresh
   search run -- and correctly proved no win through T8. A single lookahead/executor desync therefore
   turned into a lost game instead of a re-search.

## Proposal

- When the search commits a multi-phase line, record a fingerprint of the state the NEXT phase was
  planned against (hand multiset + library top-k by m_number at least; BuildDedupKey is the strong form).
- At replay, compare the real state's fingerprint; on mismatch DROP the committed line and search again
  from the real state (the line is stale; replaying it is the "deviation leaves the line stale" class).
- Ship first as a default-OFF DIAGNOSTIC (`MTG_LINE_REPLAY_LOCKSTEP_AUDIT`) that only COUNTS desyncs
  across all three tiers; any non-zero count is a lockstep defect to root-cause (as the Zenith was). Then
  the re-search as the default-ON guard.

## Latent, same class (not changed)

The same `apply_one` block pushes the definition's template (`m_number 0`) to EXILE (Living Wish's
self-exile) and to the GRAVEYARD (every resolved instant/sorcery), while the executor moves the real
copies. Nothing reads those IDs for draws today, but anything that reads graveyard card identity
(Timeless Witness's return, a graveyard-to-library shuffle, the viewer's per-copy identity, the
reference content anchoring) can disagree between the two worlds. Fixing it touches every deck's
graveyard keys, so it needs its own change and a fleet GT pass.
