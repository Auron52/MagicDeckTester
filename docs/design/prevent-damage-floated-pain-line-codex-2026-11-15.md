# Prevent Damage: floated-pain line repair

## Mechanism

At three life with Tamanoa and Rhox Faithmender, seed 30044517 turn 6 offers
Spellshock followed by Living Wish or Rolling Earthquake. The old useful-pain
payment spends both Tarnished Citadels on Spellshock and drops the second cast.
The oracle pays Spellshock with Battlefield Forge and Ancient Tomb, leaving the
Citadels for the second cast. That pair deals three total damage, but the
existing `MTG_PD_FLOAT_FLUSH` rule resolves each mana activation and Tamanoa
gain before the next activation. Each individual tap is survivable.

`PaymentPainSafe` conservatively sees an available Citadel's three-damage tap,
so the ordinary useful-pain repair limits total damage to the two life points
above zero. Its single and deeper held-source retries cannot use Forge plus
Tomb under that cap. `MTG_PD_FLOAT_LINE_REPAIR` (default on, `=0` restores the
old repair) runs only after that capped repair fails on a payment that strands
the named line. It retries source holds without the total cap, checks the rest
of the line, and runs the existing damage-event flush on a silent copy. It
keeps a payment only if that exact flush leaves the player alive. The candidate
is a payment choice; the cast menu and plan ranking are unchanged.

## Evidence

On the same candidate binary and seed 30044517, the mana oracle reports seven
APPLY-DROP flags with the lever off and zero with it on. A focused unit fixture
checks that the old arm strands Living Wish while the new arm leaves both
Citadels free, pays the Wish, and survives. The original-source full unit suite
passed 3,177 cases and 3,601,200 assertions. The combined smoke/regression run
matched every Prevent Damage row; strict reference replay reported 335 OK,
508 repaired, one beaten, and zero drift, board divergence, or enum gaps. The
six smoke and five regression mismatches outside Prevent Damage were the same
White Knights, Goblins depth-zero, and Merfolk Turns ground-truth drift already
seen before this change; no ground truth was accepted.

The same-binary held-out Prevent Damage A/B used 5,000 paired games with only
`MTG_PD_FLOAT_LINE_REPAIR` changed. Both arms averaged 5.4574, net turn delta
zero, zero faster games, zero slower games, and two digest-only changes. In
those games the new arm makes an additional Living Wish or Manabarbs cast and
wins on the same turn. Search work was 148,453,022 units off versus
148,451,140 on. `test/check_gt_logs.py` found 1,205 consistent entries and
none stale or missing.

The candidate rebased cleanly onto the incoming Merfolk Turns source as
`bb3b1059e`. Its exact PGO source stamp is `26f79285f860`. The fresh official
Prevent Damage gate on indices 47,000–51,999 reported **288 issues**:
APPLY-DROP 6, ORACLE-GAP 33, UNMODELED 12, CAPPED 237. Oracle-on/off play was
byte-identical. All six payment drops are depth one and `need_later`: seed
30050876 turn 5 has a Rolling Earthquake continuation absent from the initial
Dina plan. The deck remains beta; next fresh block 52,000–56,999. This is a
different seed block from the prior 370-issue gate, so the totals are not a
paired effect estimate. Full report:
`logs/mana_clean/pd-float-line-20261115/report.md`.

The rebased Release build and unit suite also passed all 3,177 cases and
3,601,200 assertions. The post-rebase combined smoke/regression tier ran 544
game jobs: all nine Prevent Damage rows matched. The strict viewer replay
reported 335 OK, 508 repaired, one beaten, and zero drift, board divergence,
or enum gaps. Five smoke and five regression rows still mismatch outside
Prevent Damage: White Knights and Goblins depth zero, all seen before this
change. The per-game audit found no searched win-turn regressions. No ground
truth was accepted.
