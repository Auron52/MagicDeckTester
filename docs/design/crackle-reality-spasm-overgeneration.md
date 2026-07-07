# Deferred: Reality-Spasm-funded Crackle plans over-generate at enumeration

## Status

Deferred. The standalone Crackle-with-Power targeting path (count picker + per-target
board-click) is **complete and proven exactly correct** — see the verification below. The
one remaining rough edge is the *combined* main-1 `Reality Spasm + Crackle with Power` plan,
which the human path essentially never reaches. This doc parks that edge so it isn't
rediscovered from scratch.

## What works (do NOT re-investigate)

Standalone Crackle (mana paid by real sources, no Reality Spasm) resolves **exactly
correctly** through the viewer's declared-count picker and per-target board-click. Verified
empirically on seed 2 / game 1, the turn Hinata is in play with Crackle in hand (opponent at
20, X=2 so 5X = 10 damage per target):

| Extra target the human clicks | opp life after the turn | Own Hinata | Opp 1/1 tokens |
|---|---|---|---|
| an opponent 1/1 token          | 6  (20 −10 Crackle −4 Hinata combat) | alive       | 7 (one killed) |
| own Hinata (offered last)      | 10 (20 −10 Crackle −0 combat)        | **dead**    | 8 (none killed) |

This proves **identity routing**, not just count:

- Face always takes exactly 5X.
- Each declared extra target takes exactly 5X — lethal to a 1-toughness token *and* to
  4-toughness Hinata.
- Damage lands on the **specifically chosen** permanent: clicking Hinata kills Hinata and
  spares all eight tokens; clicking a token kills exactly one token and spares Hinata.
- State-based actions correct (the dead creature leaves the battlefield to its owner's
  graveyard).
- The offered option set is exactly `CrackleExtraTargetOrder` — opponent creatures first,
  then own non-Hinata, then "You (self)" when safe (`per_target < life`), then Hinata last.
- Count cap correct: `min(X-1, legal extras)` extras beyond the face, so 1 + count ≤ X total.

The autonomous batch is byte-identical (hinata d0 seed 1001 = 608 won / avg 6.78289, digests
match ground truth) because the whole count-range + board-click path is gated on
`HumanPlayActive()` / a non-null `g_play_target_chooser`, both false in the search rollout and
the batch.

## The deferred problem

In the plan menu, a main-1 plan that casts **both** Reality Spasm and Crackle with Power in
the same turn is *enumerated* — the enumerator optimistically credits Reality Spasm's untap
"ritual" mana (Reality Spasm's untap mode is modelled as a wild-mana float, not a literal
re-tap of specific sources; see `docs/design/*reality-spasm*` and the card's `cards.json`
note). But at **apply** time the floated mana doesn't actually realize enough to pay
`{X}{X}{X}{R}{R}` for the intended X, so the Crackle cast is silently dropped and only the
combat damage lands. The declared-count picker on such a plan therefore appears to "do
nothing" to the opponent's life beyond combat.

Why it is low priority:

- Across seeds 1–7, greedy human driving **never once held Reality Spasm + Crackle + Hinata
  in hand together** — Reality Spasm gets spent early for its own mana/untap value rather
  than sitting in hand to fund a same-turn Crackle. The combined plan is a speculative
  enumeration artifact the human-driven line does not reach in practice.
- It is a *mana-affordability / enumeration* issue (the enumerator credits mana the apply
  can't realize), **not** a Crackle targeting or resolution bug. The same over-approximation
  is the known Reality-Spasm-untap-ritual modelling gap already tracked for the autonomous
  search (the "Layer-2 HinataProvider" same-turn RS→Crackle combo).

## When to pick this up

Do this together with the deferred **Reality-Spasm-untap-ritual → same-turn Crackle** search
combo (the autonomous side of the same modelling gap), since both stem from Reality Spasm's
floated-mana abstraction. The fix is one of:

1. **Tighten enumeration affordability** so a `Reality Spasm + Crackle` plan is only
   enumerated when the floated ritual mana genuinely pays the declared Crackle X at apply
   (i.e. make the enumerator's mana projection match `ApplyPlanDirect`'s realization), or
2. **De-abstract Reality Spasm's untap** into an explicit untap of specific tapped sources so
   the freed mana is real and re-tappable, then let the static planner chain it into the
   same-turn Crackle (this is the headline deferred combo and also unblocks the autonomous
   search casting Reality Spasm at all — today it is effectively a dead card).

Either way, verify with the same identity-routing method above once a combined plan is
actually castable, and rebaseline ground truth on the frozen commit.
