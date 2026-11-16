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

Raw paired data: `build/scratch/dragonstorm_storage_*.{out,err}`. The next
fresh mana-clean block for Dragonstorm is 52,000–56,999.
