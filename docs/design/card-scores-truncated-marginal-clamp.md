# `card_scores`: clamping a TRUNCATED marginal reads "no evidence" as evidence

**Status: identified, NOT fixed. The shipped instance below is a behaviour change on every deck, so
it needs a measurement before anything moves.** Found 2026-09-01 while building the Minotaur
round-4 discard arms (`minotaur-discard-policy-proposal.md`), where the same pattern was a real
defect and *was* fixed, because that consumer was an off-by-default experiment.

## The shape of the problem

`AnalyzerEngine::ComputeCardScores` builds `card_scores[name]` as a vector of per-copy marginals:

    card_scores[c][k] = avg_win_turn(k copies of c in the opening hand)
                      - avg_win_turn(k+1 copies)

and it **stops at the first copy count with fewer than `MIN_SAMPLES` (30) games**:

```cpp
if (it_k == groups.end() || (int)it_k->second.size() < MIN_SAMPLES) { break; }
```

So the vector's LENGTH is not "how many copies this card has"; it is "how many copy counts we saw
often enough to measure". A one-entry vector does not mean "the second copy is worth what the first
is". It means **we never measured a second copy**.

Every consumer that indexes past the end with a CLAMP therefore converts an absence of evidence
into a confident answer — and specifically into the *first* copy's answer, which is the most
optimistic one available.

## Why the clamp looks right (and is, for one of the two uses)

The clamp is defensible for SCORING a hand: marginals diminish, so reusing the last known one is a
safe understatement of the redundant copy's value. That is the reasoning `AIEngine::CardScore`'s
comment gives, and for `ComputeHandScore` — which sums clamped-to-zero marginals to grade a whole
opening hand — it holds.

It is NOT right when the question is **"is the k-th copy a liability?"**, because there the
first-copy value is not a conservative stand-in, it is the answer pointing the wrong way.

## The shipped instance: the bottoming tie-break

`AIEngine::CardScore(name, copy_index)` clamps:

```cpp
int idx = std::min(std::max(0, copy_index), (int)it->second.size() - 1);
```

Its consumers (`AIEngine.cpp`, the bottoming picks) call it as
`CardScore(name, copy_count(name) - 1)` and prefer to bottom the **lowest** score, among cards the
lookahead already judged win-equal. So for a card whose vector stopped at one entry, a redundant
second copy is scored at the *first* copy's marginal — and if that marginal is high, the redundant
copy is **protected from bottoming rather than preferred for it**, which inverts the intent.

Concretely on Minotaur, whose 3-ofs and 2-ofs all stop at one entry: Fanatic of Mogis records
`+0.0082` and nothing else, so a second Fanatic scores as well as the first.

**Why it has stayed invisible.** It is a tie-break among options a lookahead has already called
win-equal, so it can only reorder choices that were measured as equivalent. That bounds the damage,
and it is also why no fingerprint has ever moved because of it.

## Why this is not just "fix the clamp"

Two reasons, and the second is the one that matters:

1. It touches every deck with `card_scores`, so it is a real behaviour change, not a no-op cleanup.
   It needs the usual treatment: a behavioural diff (how often does the tie-break actually change
   the bottomed card?), then a paired A/B, then held-out confirmation.
2. **The right fix may not be at the consumer at all.** A truncated vector is a *sampling* failure —
   `MIN_SAMPLES = 30` against however many games the analysis ran. Cards at 3-of and below simply do
   not appear twice in enough opening hands. Options, roughly in increasing order of ambition:
   * consumers treat "no entry at `k`" as no-opinion (what the Minotaur arm now does) — cheapest,
     and correct for liability-style questions;
   * record the sample count alongside each marginal so a consumer can weigh rather than guess;
   * raise the analysis game count until the 3-ofs have a second-copy sample, which is the only
     option that actually *answers* the question instead of routing around it.

## Related, and the reason to be suspicious of the whole table

This deck's `card_scores` were computed by its FIRST analysis (`a43ef60f`, 2026-08-23) and that is
the only commit that has ever touched `Minotaur.profile.json` — with 127 `src/` commits since,
including the archetype provider that now makes these very decisions. `card_scores` are a generated
artifact and an engine-state fingerprint like any other, but nothing treats them as one: there is no
staleness check, and for most decks the hand-score gate is disabled
(`hand_score_threshold = -1e+18`), so the table's only live effect is this tie-break. That
combination — nearly inert, never regenerated, silently consulted — is why an error in it can sit
unnoticed indefinitely.
