# Dragonstorm: use stored mana before creature-only Territory

The fresh Dragonstorm mana-clean gate `codex-dragonstorm-lotus-20261116`
still found 126 `APPLY-DROP` flags. At seed 30,047,715, the menu offered
Unclaimed Territory, Seething Song, then Karrthus. The legal payment spends
Mountain red and Lotus Bloom black on Song, then uses Territory green on
Karrthus. The engine instead spent Territory's colorless mode on Song's
generic cost and stranded Karrthus's green pip.

The existing line reserve recognized the later green shortage and held
Territory within the ordinary land band. Mercadian Bazaar was ranked 62,
outside that band, so it still came after the held Territory. Dragonstorm's
`ManaSourceRank` now promotes a live storage land to rank 58 when the line
owes black or green and an untapped creature-only land is present. It
changes only source ordering; the storage ability and its counters retain
their existing rules. `MTG_DRAGONSTORM_STORAGE_BEFORE_CCO=0` restores the
old order (default is on).

The focused seed's one `APPLY-DROP` became zero. Seed 30,048,101 also
lost its payment drop. Seed 30,047,958 still has two Karrthus drops, and
seed 30,048,391 still has complex ordering drops, so this addresses one
payment pattern rather than the whole cluster.

Paired Release games were play-identical: 1,000 pilot games on seeds
9,600,001–9,601,000 and 5,000 held-out games on seeds
9,700,001–9,705,000 had no changed win turn. In the held-out pair both
arms won 4,957 games; search units were 8,661,651 off and 8,660,817 on.
The Release unit suite passed 3,353 cases and 3,614,055 assertions.

The fresh PGO mana-clean gate `codex-dragonstorm-storage-20261116` then ran
indices 52,000–56,999. Oracle-on/off play was byte-identical. It found 115
issues: 79 `APPLY-DROP`, 26 `APPLY-DROP-ORDER`, 6 `ENUM-GAP`, and 4
`ORACLE-GAP`; no unmodeled or capped decisions. The largest remaining
payment group is Apex of Power into Karrthus or Kolaghan. The exact report
is `logs/mana_clean/codex-dragonstorm-storage-20261116/report.md`; the next
fresh block is 57,000–61,999.

Raw paired data: `build/scratch/dragonstorm_storage_*.{out,err}`.

An unshipped fixed-order trial moved the cheapest ritual just after Apex
in both the Dragonstorm menu and the sequential feasibility walk. It was
default off and tested on enum-gap seeds 30,053,057, 30,042,074, and
30,044,871. All three still reported `ENUM-GAP`; the rejected subsets
still had `ef-walk-fail`. The walk's payment choice remains the obstacle,
so the extra order was removed before any paired game run.
