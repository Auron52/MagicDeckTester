# Boars: bound the oracle's rejected-plan target fan

The first four-deck mana gate (`codex-never-four-20261116`) could not finish.
Its 32-worker oracle arm reached the 40 GB RSS guard after four Boars chunks;
resumes at 16, eight, and four workers also reached the guard on game index
301. A single replay of seed 30,000,301 reached 40.68 GB. The off control has
a separate Merfolk Turns long-game RSS problem. None of these incomplete runs
is a clean gate.

The focused Boars turn-six main-one decision has 104 menu plans. The oracle's
first menu plan audits successfully, then its mana-gate observation enumerates
about 20,384 base plans before `AppendSubdecisionAxes`. Raggadragga's
per-trigger target axis multiplies those rejected base plans before the
oracle's priced decision cap can poll. A five-second decision limit, a
ten-node payment limit, and a one-menu-plan limit all still exceeded 10 GB.

The observer now stops that target-axis fan before it exceeds 50,000 variants
and reports `CAPPED-DECISION` for the unverified rejected plans. Normal search
and play never enter this guard. With default oracle settings and a 10 GB RSS
guard, the exact game finishes in 1.6 seconds and reports one explicit cap,
zero RSS warnings, and zero other misses. The oracle-off replay has the same
turn-six result and work counters. The full Release unit suite passes 3,362
cases and 3,614,418 assertions.

This is a memory safety correction, not a clean Boars verdict. A lossless
streaming or identity fold for the observed target fan is still required to
audit that decision fully. The partial Boars flags also include cast-order and
payment drops. The Merfolk Turns off-control RSS growth needs its own analysis.
