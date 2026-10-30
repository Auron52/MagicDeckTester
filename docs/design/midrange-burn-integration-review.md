# Midrange Burn integration review

2026-10-29. Live Scryfall oracle fetched with curl for Chain Lightning, Rift Bolt, Skewer the Critics, Price of Progress, Fireblast, Magma Jet, Grim Lavamancer and Searing Blaze. This review inspected integrated primary source; no builds or tests were run by this reviewer. Parent owns runtime validation. Lavamancer fuel-token thread determinism is owned separately by the permanent agent.

## Confirmed defects corrected during review

| Defect and reproduction | Correction and coverage |
|---|---|
| `EffectHandler::PushFreeCast` constructed an empty-target stack entry. A cascade/free-cast Lightning Bolt counted as cast but resolved zero damage; rollout hand recursion dealt three. | Prepare a stable fixed-burn target/pair before cast counters/triggers and attach its pin to the real stack entry. Added actual PushFreeCast→Resolve Bolt test and Blaze legal-pair/no-target tests. Nonfixed free-cast handling unchanged. |
| Rollout hand `[LUTS-staged Rift Bolt, actual-hand Rift Bolt]` selected a legal actual-hand suspend action, but ApplySuspend's name-only scan exiled the first staged copy. | Shared ApplySuspend now excludes staged exile entries before payment. Added staged-first mixed-copy and exile-only tests. |
| A Magma Jet target disappearing before resolution caused zero damage but still resolved Scry 2. With all targets illegal CR 608.2b forbids every part of resolution. | Shared fixed-burn resolver returns whether any target remains legal. Executor graveyards a fizzled spell before riders; rollout skips riders/scry. Added Magma actual-stack fizzle test and Blaze one-survivor/all-illegal tests; existing human-pair tests cover a surviving walker separately. |
| Suspend arrival selected Rift Bolt's target after on-cast triggers. A Chandra-emblem trigger could remove the original intended target, and the arrival incorrectly selected a fresh target afterward. | CastOffSuspend prepares a stable target before cast counters/triggers; resolution uses that saved target and its normal fizzle result. Added hook test proving the target prompt sees zero casts and untriggered Swiftspear prowess, then the cast increments and prowess resolves. |
| A suspend arrival ignored the current cast restriction budget. | A zero budget strands the card in exile without time counters and does not cast/count it. A legal cast spends the budget and installs max_casts_after consistently. Added zero-budget test. |

## Findings checked without claiming defects

- Fireblast's `fireblast:first,second|@burn/...` composition is safe: prefix detection still works and `atoi` stops before the burn suffix. Both normal mana-paid and sacrifice-two-Mountains actions are emitted. Alternative victims include tapped Mountains and nonbasic Mountain lands. The `a.def` field is populated before the final burn-target fan, so the alternative is not accidentally omitted there.
- A possible missing newly played Mountain was investigated and disproved: EnumeratePlansWithLandUncached plays the selected land on a copy before EnumeratePlans collects the Fireblast alternatives. Thus one Mountain in play plus this turn's Mountain drop can reach the two-Mountain alternative in the post-land branch.
- Chain Lightning copies use the actual damaged target's controller, never trigger prowess/storm/cast abilities, and retarget through the shared player/creature/walker resolver. The passive opponent declines its optional RR payment. A human who damaged themselves or their permanent gets the pay/copy/target decisions.
- Magma Jet's ordinary successful resolution deals target damage before Scry 2 in executor and rollout. The new all-illegal guard specifically closes its exception.
- Price of Progress counts each represented player's current nonbasic lands independently, rather than multiplying the opposing player's land count by opponent heads. The 2HG game has aggregated player/board representation: do not invent a second copy of the same lands. Parent is independently wiring its noncombat-damage notification and self-loss rules.
- Rift Bolt suspension is not a cast; its eventual arrival is a cast and fires on-cast triggers/prowess before the spell resolves. The optional decline remains stranded in exile with no counters and is not repeatedly offered.
- The full fixed-burn target fan is provider-owned: Midrange opts in; historical providers keep their existing target policy; human/unpruned paths expose the full rules-legal axis. No global adoption measurement is inferred.

## Remaining validation requirements

Compile the integrated result and run the focused spell, permanent, targeting and mana tests. Then run coverage/field/cost/viewer gates and the required mismatch/depth/Claude-play convergence workflow before treating the deck as analyzed. Check the newly integrated Chandra emblem specifically: choose a spell's creature/walker target first, kill it with its emblem trigger, then verify that the spell fizzles without its extra rider. The hook test pins target timing but does not replace this interaction scenario. Verify Price and other burn source histories against the parent-owned Incinerator queue, including simultaneous Blaze damage and spell-resolution-boundary trigger flushing.
