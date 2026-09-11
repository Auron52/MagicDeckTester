# Filling in the missing mulligan-generation settings (queued 2026-09-11, user request)

`value_play.mull_gen_depth` / `mull_gen_budget_ms` tell the mulligan generator which (depth, budget)
to LABEL its keep table at. Absent => generation silently inherits the deck's PLAY settings. That is
the correct answer for some decks and an expensive accident for others, so it is derived by
measurement (`scripts/derive_mullgen_setting.py`), never defaulted.

User framing: *"fill in all of the missing mulligan settings. The trusted value-leaf trust cases
should potentially be left as-is."* That instinct matches the tool's own rule 1 verbatim.

## The two rules (from the script's header, both measured)

1. **TRUSTED at the shipped play depth -> emit NO override.** Generating at play settings is not
   merely acceptable, it is frequently the CHEAPEST arm available (slivers 1,496 units/rollout vs
   2,431 for d3 b3) AND perfect by construction: reaching the value leaf terminates the line instead
   of playing the game out. No trade-off to arbitrate.
2. **Otherwise -> the CHEAPEST (depth, budget) pair clearing a rank-fidelity floor.** Cost is NOT
   monotonic in depth (slivers d3 b20 = 11,551 units vs d5 b20 = 1,496), so a default is a guess and
   a wrong guess costs hours-to-days of generation.

## Fleet survey, 2026-09-11

Has settings: breaching 4/3, cgiving 1/3, critter 1/3, dragons 1/3, fivecolour 2/1, goblins 3/3,
kitty 3/3, melira 3/3, minotaur 1/3, mirrorwing 2/3, stompy 3/3.

Missing (9), split by whether trust is EFFECTIVE -- `leaf: "none"` zeroes `value_trust_depth` in
`apply_leaf_policy()`, so a leafless deck is never a trust case no matter what its sidecar says:

| leave as-is (rule 1) | trust | plays | | derive (rule 2) | why not trust |
|---|---|---|---|---|---|
| Auras | 5 | d5/b20 | | Anti-Lifegain | no trust depth |
| Knights | 5 | d5/b20 | | Hinata2 | no trust depth |
| burn | 5 | d6/b20 | | Dragonstorm | `leaf: none` |
| slivers_vial | 5 | d5/b20 | | Fluctuator | `leaf: none` => trust 4 inert |
| | | | | treasure_hunt | `leaf: none` |

## MUST run AFTER the shape adoptions, not before

The derivation scores candidate labellers against **the deck's own shipped play policy**. Every
2026-09-11 `leaf: none` adoption CHANGED that policy, so a setting derived before the adoption was
fitted to a deck we no longer ship. Two consequences, both in scope for this pass:

* **Re-derive, don't just fill in.** dragons 1/3, minotaur 1/3, mirrorwing 2/3, stompy 3/3,
  goblins 3/3 and kitty 3/3 were all derived against a model-leaf play policy. The leafless
  transform also zeroes trust on goblins (6) and minotaur (5), which moves those decks from rule 1's
  neighbourhood into rule 2 outright.
* **critter looks rule-1-inconsistent today**: trust 5 at play d5 with a live model leaf, which rule
  1 says wants NO override, yet it ships `1/3`. Either the trust is not actually reached at play
  depth or the setting predates the rule. Worth resolving in the same pass rather than carrying it.

Same ordering applies to anything adopted from the 2026-09-11 modes screen (Hinata2, Kitty): adopt
the shape first, then derive the labeller against the shape we actually ship.

## Cost / sequencing

One pooled batch, serial with respect to every other generation stage (per CLAUDE.md: profile ->
value leaf -> mulligan, one at a time on the box). The derivation itself is openers x R scoring, not
a full generation, so it is cheap relative to the keep-table generation it configures. Nothing here
regenerates a keep table: the output is sidecar keys. Regenerating the tables against the new
settings is a separate, later decision.
