# Dragonstorm: Ruby order, paired Apex mana, and storage burst

The Dragonstorm mana-clean gate on indices 62,000–66,999 reported four
`APPLY-DROP-ORDER` flags, one `ENUM-GAP`, and two `ORACLE-GAP` flags alongside
86 other payment drops. This pass repairs those first three classes. The
source gate is `logs/mana_clean/codex-dragonstorm-visible-lump-20261117/`.

## Ruby Medallion order

In seeds 30,064,556, 30,064,889, 30,065,592, and 30,066,357, the
provider's projected order cast Ruby before the cheap rituals and used the
red land that those rituals needed. A later Ruby slot pays every cast. The
Dragonstorm provider now tries a deterministic permutation of the opening
cheap rituals and Ruby, only when its chosen order fails both real payment
probes, a later creature is present, and the sequence is small. It accepts
the first order that the sequential payer proves payable. This is an order
choice in the provider, not a searched branch or a generic engine prune.
`MTG_DRAGONSTORM_RUBY_FUNDING=0` restores the previous order. All four
exact repros clear with the rule on and return with it off. In 5,000 paired
Dragonstorm games, on and off had identical win turns and play digests; the
guarded version added about 0.8% CPU time and 1.6% search units.

## Two visible Apex casts

Seed 30,062,821 has two Apex of Power copies already in hand. The first
hand cast produces ten mana after resolution, which can pay the second hand
cast. The selection-exact mana gate had not credited this visible output,
so it rejected the payable ritual and double-Apex plan before the exact
walk could inspect it. The gate now adds the Apex credit only when two
unstaged copies of the same mana-producing spell are visible in hand. The
sequential walk applies the first hand-cast Apex's chosen-color mana before
testing the next payment. It does not assume anything about the cards Apex
will exile. `MTG_APEX_VISIBLE_LINE_MANA=0` is the control. The human viewer
menu is excluded because broadening its probe changed a saved reference's
option order; strict replay is unchanged with the autonomous rule.

The exact seed now has no enum or oracle gap; disabling the selection gate
also recovers the line. A broad trial credited every hand Apex and cost 22%
more CPU in 5,000 paired games. The two-copy condition cut that to 5.0%
more CPU and 1.37% more search units on the same block (seeds
9,810,001–9,815,000). Both arms won 4,967 games; the narrowed arm won two
games one turn earlier, none later, and changed 107 play digests. Its
loss-penalized average was 4.3652 versus 4.3656 off.

## Storage land backtracker

Seed 30,062,628 exposed a mismatch between the ordinary payer and the
backtracker used by its fallback and cache. An ordinary Dwarven Hold or
Mercadian Bazaar tap releases all live counters into float; the backtracker
released only the current payment's shortfall. The backtracker and cached
replay now release the same full burst, including surplus for later casts.
`MTG_STORAGE_BACKTRACK_FULL_BURST=0` restores its old behavior. Both exact
oracle gaps clear. A focused test covers both lands with three counters,
a two-mana bill, one red floating afterward, and cache replay.

## Validation and remaining queue

The Release suite passed 3,383 cases and 3,614,946 assertions. Combined
Dragonstorm/Shadowborn smoke and regression ran 5,762 games; Shadowborn was
unchanged, no searched Dragonstorm game changed win turn, and all 561
scenarios passed. Strict viewer replay returned 329 ok, 534 repaired, one
beaten, and zero play drift, board divergence, or enum gaps across 864
references. Both suboptimal references remain beaten. The nine changed
Dragonstorm ground-truth logs were accepted. Its four depth-zero turn
changes came from the previously shipped visible-Lotus rule: turning off
the new storage burst left those four unchanged, while turning off
`MTG_ONDEMAND_VISIBLE_LUMP` restored the old turns. The Ruby and Apex
changes do not apply to the depth-zero search cell.

The earlier block's remaining 86 `APPLY-DROP` reports span 41 signatures. The most common
Karrthus/Apex case, seed 30,065,244 turn six, still has 24 reports. Its
successful oracle line preserves black and green across the Apex
continuation. Swapping the order of Bloom colors in the existing payer
did not clear it; that trial was removed. A fresh optimized mana-clean
block after this pass was required and has now completed.

The fresh PGO+LTO gate on indices 67,000–71,999 completed at source
`7940651a93b1` with 157 issues: 121 `APPLY-DROP` across 40 signatures,
four `APPLY-DROP-ORDER`, four `ENUM-GAP`, and 28 `CAPPED-DECISION`.
There were zero `ORACLE-GAP` flags and oracle-on/off play was byte-identical.
The new seed block is not directly comparable to the earlier block's issue
count. See
`logs/mana_clean/codex-dragonstorm-ruby-apex-storage-20261117/report.md`
for exact new repros, including seeds 30,068,634, 30,070,527, 30,068,175,
30,070,173, 30,068,752, and 30,071,498. The queue remains open.

## Seething Song funds Ruby

The fresh gate's four `APPLY-DROP-ORDER` flags are two plan variants each
in seeds 30,068,752 turn three and 30,071,498 turn four. The first needs
Seething Song before Ruby Medallion; the second needs Rite of Flame and
Seething Song before Ruby. The existing Ruby fallback handled only a run
of two or more rituals with printed cost at most two, so neither case
entered it. The provider now tries moving Ruby after a contiguous ritual
run containing Song, before Irencrag Feat, when fewer than two cheap
rituals precede Song, a later creature is visible, and the original order
fails both payment probes. It adopts the move only if the sequential payer
proves the complete new order. This is one provider order choice, not a
new searched branch.

Both exact games now report zero `APPLY-DROP-ORDER`; turning off
`MTG_DRAGONSTORM_RUBY_FUNDING` restores two flags in each. The four older
Ruby repros remain clear. Release units passed 3,383 cases and 3,614,946
assertions. Combined Dragonstorm/Shadowborn smoke and regression passed all
cells, 561 scenarios, and 864 strict viewer references with zero drift,
board divergence, or enum gap. In 5,000 paired held-out games at seed
9,820,001, both arms won 4,957 games with identical per-game win turns.
The flag-off arm disables the earlier Ruby fallback as well, so its
493.34 CPU seconds versus 503.26 seconds with the combined rule on
(+2.0%) does not isolate the new move's cost.

The PGO+LTO build at source `fcb9352608ea` passed. Its fresh 5,000-game
gate on indices 72,000–76,999 found 28 `APPLY-DROP` flags across 16
signatures, and no ENUM-GAP, APPLY-DROP-ORDER, ORACLE-GAP, or capped flag.
Oracle-on/off play was byte-identical. This is a new seed block, so its
count should not be read as a direct reduction from the previous block.
The leading residuals are seed 30,075,408 turn four (Apex continuation
loses Kolaghan, six flags) and seed 30,073,082 turn five (Apex/Scourge
continuation loses Karrthus, ten flags across variants). Other useful
exact repros are 30,072,155 turn five (Ruby → Song → Karrthus),
30,074,224 turn five (Song → Karrthus), 30,074,081 turn four (Karrthus
→ Scourge), and 30,076,435 turn four (Desperate → Feat → Karrthus).
The gate record is
`logs/mana_clean/codex-dragonstorm-ruby-song-20261117/report.md` and
`test/mana_clean.json`. The payment queue remains open.
