# Prevent Damage: terminal outcomes and payment-table cap (2026-11-16)

The fresh `codex-pd-scripted-20261116` mana-clean gate found 33 flagged games
in games 92,000–96,999. There were no dropped actions or enumeration gaps;
oracle-on/off play was byte-identical. Twenty-four oracle-gap flags were two
terminal-loss signatures, seven unmodeled flags were one capped payment-table
decision, and two oracle-gap flags were a live Pyrohemia continuation.

The oracle formerly compared all late state fields after the active player had
already lost. The damage model parks life at `kLostLife`; a scripted replay
that pays one more pain land can end at `kLostLife-1`, and its later library
order or battlefield may differ. Those are the same terminal loss, unless the
opponent also lost (a draw). The cover check now accepts a same-line oracle
outcome when both sides have lost and agree on whether the opponent also lost.
The ordinary resource comparison runs first, so a surviving improvement can
still be reported. Focused replays of seeds `30094659` and `30094134` now have
zero oracle gaps (previously 18 and six respectively). The live Pyrohemia
seed `30095610` still reports its two oracle gaps.

The two live Pyrohemia gaps exposed another audit-context omission. At a
breakpoint continuation, two payment histories reached the same visible board
and `{1}` ability bill, but the continuation had chosen different creatures for
Dina to sacrifice. The frontier prune merged them because it did not retain
`PaySacSpareScope`'s source and victim. Capturing those IDs in the oracle hook,
keying frontiers by them, and restoring the scope while enumerating payment
options clears both seed `30095610` and the earlier seed `30089526`. The focused
oracle work increases by only 14 and eight applies respectively. The initial
trial that keyed every creature-ability history also cleared the gaps but cost
2,442 and 13,052 extra applies, so it was removed.

The payment table stopped after 65,536 probes in Prevent Damage seed
`30094874`, turn 8. A focused replay at 131,072 probes completed 124,660
options in 12.98 oracle seconds, with zero unmodeled flags or other gate
issues. The default table ceiling is now 131,072; the existing environment
override remains available for diagnosis.

The Release unit suite passes 3,359 cases and 3,614,281 assertions. The
combined Prevent Damage smoke/regression gate ran 2,560 games; all nine solo
`.wins` files match the saved pre-fix same-binary control byte-for-byte. The
same known ground-truth mismatches remain. Strict viewer replay reports zero
play drift, board divergence or enumeration gaps among 864 references, and
both require-beat cases passed. The final conditional-scope optimization was
rebuilt and rechecked: seeds `30095610`, `30089526`, and `30094874` still have
zero oracle gaps, unmodeled flags, and caps; all 3,359 unit cases pass on that
exact build.

Fresh PGO mana-clean gate results: pending.
