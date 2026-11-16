# Handoff: the mana-clean fix chain, Claude session -> Codex (2026-11-14 ~10:45 UTC)

The Claude owner session (24b9e9dc) is out of usage and hands the USER's standing mana-clean program to Codex.
Detailed per-item history is in `docs/design/mana-clean-pass-checkpoint.md` (newest bullets at the end of each
round). Per-item handoff files (gitignored, local only) are in `logs/mana-clean-chain/<item>-handoff.md`.

## Codex continuation (2026-11-16)

The fresh Spirits PGO gate on games 20,000–24,999
(`codex-spirits-souls-hand-20261116`) has twelve
`APPLY-DROP-ORDER` flags in seed 30,021,365: a Vialed White Orchid
Phantom destroys our Azorius Chancery before Millicent pays. The same
actions pay with Millicent before the Vial put. No other issue class
appears, and oracle-on/off play is byte-identical. The record is in the
gate report and `test/mana_clean.json`. A narrow Spirits order rule now
moves Millicent before that Vial put only when the sequential payment
probe accepts it; the exact seed has zero issues, and the off control
restores all twelve. Two thousand paired Spirits games have identical win
turns and play digests, with 12 extra work units out of 77.9 million.
Spirits/2HG combined smoke and regression pass all ten cells with no
per-game changes; all 561 scenarios pass, and strict viewer replay has
no gap or drift across 864 references. The full unit build and a fresh
PGO gate remain.

The fresh Spirits PGO gate on games 15,000–19,999 after the Souls
flashback hold (`codex-spirits-souls-line-20261116`) found ten payment
drops in one signature: hand-cast Lingering Souls `{2}{W}` before White
Orchid Phantom `{W}{W}` on seed 30,015,009. Oracle-on/off play is
byte-identical; there are no other issues. The record is in the gate
report and `test/mana_clean.json`. The follow-up held route clears exact
seed 30,015,009 (ten drops to zero); its focused rollout/executor test
fails with the switch off and passes with it on, and all 3,369 Release
unit cases pass. Another 2,000 paired Spirits games change no win turn
or play digest (+0.01% work). The three old flashback repros remain clear;
Spirits/2HG combined smoke/regression pass all ten cells, 561 scenarios
pass, and strict viewer replay has no gap or drift across 864 references.
A fresh block remains.

The 61 Spirits payment drops on games 10,000–14,999 now clear in all
three exact repro games. Souls flashback's generic pip needed a different
held-first route before Phantom or Millicent, including a line with
already-floating W/U. `MTG_SPIRITS_SOULS_LINE_HOLD=0` restores all 61
original drops; full Release units pass. A 2,000-game paired Spirits check
changed no win turn, play digest, or total work. Spirits/2HG combined
smoke/regression passed all ten cells, 561 scenarios passed, and strict
viewer replay had no drift or gap across 864 references. A fresh gate
is next. See
`docs/design/spirits-souls-line-payment-2026-11-16.md`.

The fresh Spirits PGO gate on games 10,000–14,999
(`codex-spirits-karoo-spare-20261116`) is complete with byte-identical
oracle-on/off play. It has 61 `APPLY-DROP` flags in seven signatures,
mostly Lingering Souls flashback followed by White Orchid Phantom; there
are no oracle gaps, enum gaps, or caps. Its source record is in
`logs/mana_clean/codex-spirits-karoo-spare-20261116/report.md` and
`test/mana_clean.json`. The older Chancery/Island Supreme Phantom → Usher
oracle gap is separately fixed in the audit's line-context replay; all
three exact gate repros clear with that fix, and the off-control restores
the original gap. Spirits still needs the remaining flashback payment
drops repaired and another fresh gate.

Spirits' remaining Vial → White Orchid Phantom payment drops used a
creature-only white land for Vial's generic pip while Chancery sat untapped.
The scoped scarce-color mask had held the whole Chancery even though its blue
unit could pay Vial and its white unit could float for Phantom. The payer now
releases a held fixed two-color Karoo only when one color is spare for a
one-generic bill. The focused rollout/executor test and all three exact gate
repros pass; a 2,000-game paired Spirits check changes no win turns or play
digests. Combined smoke/regression had no searched changes and one faster
depth-zero game; viewer and scenario checks passed. See
`docs/design/spirits-karoo-spare-generic-2026-11-16.md`. Its fresh PGO gate
is recorded above.

The fresh Morph PGO gate on games 5,000–9,999 completed after the combat-hold
frontier fix (`codex-morph-combat-hold-20261116`). Oracle-on/off play is
byte-identical. The gate still reports 1,627 issues: 150 `APPLY-DROP`, 429
`DOMINATED-CREATURE`, 1,046 `ORACLE-GAP`, and two `CAPPED`. The remaining
payment and frontier clusters require follow-up; the exact report and source
record are in `logs/mana_clean/codex-morph-combat-hold-20261116/report.md`
and `test/mana_clean.json`.

Morph's combat-step Berserk payment temporarily holds its attacker tapped.
The audit's frontier prune had merged that board with one where the creature
was permanently tapped for mana, losing the legal two-land route. The held
attacker now separates those frontiers; a focused test fails before and
passes after, full units pass, and seed 30,000,074 drops 22 oracle gaps to
zero. The first 500 Morph games fall from 290 to 99 oracle gaps. See
`docs/design/morph-combat-hold-frontier-2026-11-16.md`; its fresh PGO gate
is recorded above.

The fresh Spirits PGO gate on games 5,000–9,999 is complete
(`codex-spirits-post-holds-20261116`), with byte-identical oracle-on/off
play. It has 56 issues: 35 `APPLY-DROP` flags, all an Aether Vial/White
Orchid Phantom order paying the Vial with a white source that Phantom needs;
and 21 `ORACLE-GAP` flags, all Supreme Phantom/Usher of the Fallen lines where
the oracle loses the engine's payment using Azorius Chancery and Island.
There are zero enum gaps, unmodeled flags, or caps. Exact counts and repros
are in `logs/mana_clean/codex-spirits-post-holds-20261116/report.md` and
`test/mana_clean.json`. Spirits remains unclean; both signatures need fixes.

Spirits' Souls flashback before White Orchid Phantom spent the only flexible
white source on Souls' generic pip and dropped Phantom. The existing
held-first scarce-colour mask clears all nine drops in the first 500 Spirits
games, but its global setting loses games elsewhere. Spirits now opts into
that mask through its provider; `MTG_SPIRITS_SCARCE_COLOR_MASK=0` restores the
old payer. Fresh 5,000-game paired Spirits play changed one digest and zero
win turns with flat work. The scoped build clears the focused seed, full
3,365-case units pass, Spirits/2HG smoke and Spirits regression are
byte-identical, and strict viewer replay passes 864 references. See
`docs/design/spirits-scarce-colour-line-hold-2026-11-16.md`.

Spirits' same-main Souls hand cast → Vialed Geist → Souls flashback was
rejected by the aggregate mana gate because its pending graveyard-cast
candidate had no definition pointer and received no Geist discount. Resolving
the definition by interned name clears the exact repro and all five enum gaps
in the first 500 gate games. A fresh 5,000-game paired A/B changed 12 play
digests and zero win turns; full units, 561 scenarios, strict viewer replay,
and Spirits/2HG smoke passed. One searched regression game gained a turn and
one lost a turn through budget churn that recovers at 4×. Ground truth is
accepted with the audit note. See
`docs/design/spirits-pending-flashback-geist-credit-2026-11-16.md`.

The first fresh PGO mana gate for Morph and Spirits is complete on games
0–4,999 for each deck (`codex-morph-spirits-first-20261116`). Oracle-on and
oracle-off play is byte-identical. Morph has 3,647 issues: 162 payment drops,
424 creature-dominance flags, and 3,061 oracle gaps; it has no enum gaps.
Spirits has 86 issues: 31 enum gaps, 27 payment drops, and 28 oracle gaps.
Spirits' enum gaps concentrate on same-turn Lingering Souls hand cast,
Patrician Geist Vial put, and discounted flashback; its payment drops include
White Orchid Phantom followed by Souls flashback. Both remain beta. Exact
repros and counts are in `logs/mana_clean/codex-morph-spirits-first-20261116/report.md`
and the tracked `test/mana_clean.json` record.

Boars' Greaves-in-hand order now moves a fresh mana creature ahead of Herald
when Greaves can haste it in the same visible line. Exact seed 30,005,044
clears 23 order drops; on the first 500 fresh-block games, order drops fall
107→41 and enum gaps 12→2. A fresh 2,000-game paired solo A/B gains one
turn and loses none. Boars/2HG searched smoke and regression have no slower
games; three depth-zero games lose one turn through fewer Herald counters.
See `docs/design/boars-greaves-hand-order-2026-11-16.md`.

Fresh PGO mana gate `codex-boars-mill-red-fresh-20261116` on indices
5,000–9,999 is complete with byte-identical oracle-on/off play. Red Elementals
is clean (zero issues). Mill has 113 `ENUM-GAP` flags, all involving Visions of
Beyond plus Surgical Extraction; it has zero oracle gaps or payment drops.
Boars has 3,258 issues: 19 enum, 215 payment drops, 667 order drops, 1,810
creature dominance, 193 haste dominance, 44 oracle gaps, one unmodeled tap,
and 309 decision caps. Both remain unclean. The exact record is
`logs/mana_clean/codex-boars-mill-red-fresh-20261116/report.md` and
`test/mana_clean.json`.

Boars' double-Decimator `APPLY-DROP` cluster came from spending restricted
green on the first spell's generic pips while leaving a general red unit
unused. A visible-line payer rule reserves that green for the second spell:
the exact seed clears and first-500 Boars drops fall 139 -> 36. A fresh
2,000-game paired solo A/B changed seven play lines and no win turns. The
selected cross-deck regression gained 23 searched games, with one Soldiers
budget-churn slowdown that recovers at 4× budget; ground truth is accepted.
See `docs/design/boars-creature-float-later-color-2026-11-16.md`. Boars
still requires fresh 5,000-game gates and repairs for its other flags.

Boars' approved Greaves exception now ranks a freshly cast Sage or Zhur-Taa
ahead of Flinthoof only while Greaves is already on the battlefield. This
reduces the first 500 Boars order flags 99 -> 69, clears 22/23 on seed
30,000,239, and changes no win turn in a fresh 2,000-game paired solo check.
Boars/2HG smoke and regression have no slower games and one faster two-headed
game; the changed ground truth is accepted after audit. See
`docs/design/boars-haste-dork-order-2026-11-16.md`.

The first complete Boars/Mill/Red Elementals gate (5,000 games each) found
Boars 3,542 issues, Mill 360 issues, and Red Elementals clean; oracle-on/off
play was byte-identical. Mill's Surgical life-only cast was incorrectly
removed by a dominance rule assuming a mana-paid twin. A narrow guard fixes
that precondition, reducing the first 500 Mill games from 18 to 12 enum gaps.
The separate Surgical mana-cost trial leaves eight gaps and has no measured
speed gain. See `docs/design/mill-surgical-mana-clean-2026-11-16.md`.

The first pooled gate for never-checked decks stopped at the 40 GB RSS guard.
Boars seed 30,000,301 alone allocated over 40 GB while the oracle observed
mana-gate-rejected plans. The Raggadragga per-instance target fan expanded
roughly 20,000 observed base plans before the priced cap could poll. An
observer-only 50,000-variant safety bound now emits `CAPPED-DECISION` and
lets that seed finish under a 10 GB guard; the gate remains failed. The
Merfolk Turns off arm has a separate long-game memory problem. See
`docs/design/boars-observed-axis-memory-cap-2026-11-16.md`.

Shadowborn's fresh gate on indices 95,000–99,999 found 48 order drops in one
Stone/Apostle/Ritual/multiple-Intent game and three Lotus-timing payment drops
in another. A visible, sequentially payable Apostle-before-Ritual arrangement
clears the 48 exact flags without adding a searched order. Its 5,000-game
paired A/B changed one play digest and zero win turns. Full units, all
Shadowborn/2HG regression cells except one depth-zero smoke digest, scenarios,
and strict viewer passed; the single changed game was traced and its smoke
ground truth accepted. The remaining Lotus-timing question still awaits the
user's policy answer. See `docs/design/shadowborn-two-lake-oracle-pin-2026-11-16.md`.

Dragonstorm's splice-reveal repair (`1086ba6c7`) checks that a Desperate
Ritual being spliced has another physical copy still in hand; Apex-staged
copies cannot serve as reveals. Seed 30,054,701 now has zero flags. The
5,000-game paired A/B had two one-turn slowdowns, both removing illegal old
win lines, and no faster games. The 3,360-case unit suite, 561 scenarios,
and strict 864-reference viewer replay passed. Dragonstorm-only smoke and
regression ground truth was accepted after a per-game audit found no slower
searched win. The fresh PGO gate on indices 57,000–61,999 found 63 issues:
61 payment drops and two order drops, with zero enum or oracle gaps and
byte-identical oracle-on/off play. The next block is 62,000–66,999; see
`docs/design/dragonstorm-splice-hand-legality-2026-11-16.md`.

The pooled fresh PGO gate `codex-mana-queue-20261115` is complete on all
four decks, with byte-identical oracle-on/off play. It found Gods 4
`APPLY-DROP` plus 10,483 unaudited-plan `CAPPED-DECISION` flags,
Prevent Damage 5 drops / 73 oracle gaps / 26 capped-decision flags,
Shadowborn 4 drops, and Dragonstorm 3 enum gaps / 147 drops / 136
drop-order flags / 12 oracle gaps. None is clean. Source and exact next
blocks are in `logs/mana_clean/codex-mana-queue-20261115/report.md` and
`test/mana_clean.json` (record commit `2422ffc27`).

The fresh Shadowborn block 85,000–89,999 found seven payment drops, two
order drops, and one oracle gap, with byte-identical oracle-on/off play. The
oracle gap was a two-Lake Abhorrent Overlord payment: the engine's ordinary
two-crack path kept the early-crack pin while the oracle cleared it. A
script-only correction and focused unit assertion clear seed 30,089,424; the
3,360-case Release unit suite passes. See
`docs/design/shadowborn-two-lake-oracle-pin-2026-11-16.md`.
The Shadowborn/Shadowborn2HG smoke and regression rerun passed all 11 jobs,
561 scenarios, and 864 strict viewer references with zero play changes. The
two-Lake oracle fix is committed as `b0c9fbfd0`; the next fresh Shadowborn
block is 90,000–94,999. Shadowborn seed 30,085,008 and Dragonstorm seed
30,060,428 both need an early Lotus crack to preserve a later cast, even
though the immediate spell is payable without it. A user question asks
whether this counts as on-demand; no payment-policy change is shipped.

The next fresh Shadowborn PGO block, 90,000–94,999, was oracle-on/off
play-identical and reduced to seven payment drops in two signatures; it had
zero oracle, enumeration, and order gaps. One drop names Ritual, Culling,
and Sire in the original plan. The activation reserve keeps a Swamp for a
later Apostle activation and forces Ritual to use the only red land,
stranding Sire. A visible same-plan unique-colour hold clears focused seed
30,092,453. All 3,361 unit cases and Shadowborn/2HG combined regression
pass. In a 20,900-game pooled paired A/B, 5,000 fresh Shadowborn games and
the searched cells were play and win identical; three Selesnya depth-zero
play digests changed with no win-turn change. The full cross-deck suite
replayed 864 strict viewer references cleanly, but did not rebaseline its
123 broad ground-truth mismatches, mostly previously documented drift.
Six remaining drops are the post-Intent Lotus timing tradeoff. A fresh
PGO Shadowborn gate is next. See the updated Shadowborn two-Lake document.

The Gods Study → Loam green hold fixes the two focused payment drops, but
failed the 5,000-game adoption bar: two slower games, none faster, and
12.1% more recorded game time. It remains default off; the USER tradeoff
question is pending. See `docs/design/gods-loam-line-green-hold-2026-11-16.md`.

The Dragonstorm Lotus black + surplus-generic fix clears all four drops
in seed 30,042,142. The 5,000-game Dragonstorm A/B had three faster and
none slower; ten mana-sensitive decks were play-identical in a 500-game
cross-deck screen. Combined smoke/regression showed the existing 71
ground-truth drift cells; only one of 15 off-control rows differed, the
Dragonstorm depth-zero smoke improvement. The strict viewer had zero drift
and zero enum gaps in 864 references. Source `732437b0c` is committed,
default on. The fresh PGO gate 47,000–51,999 completed: oracle-on/off play
is identical; 287 issues remain (3 enum gaps, 126 drops, 121 drop-order,
37 oracle gaps), so Dragonstorm remains beta. The next block is
52,000–56,999. Most drops are Kolaghan/Karrthus lines using creature-only
Unclaimed Territory. See `docs/design/dragonstorm-lotus-black-generic-2026-11-16.md`.

Dragonstorm's next payment fix puts Mercadian Bazaar ahead of a reserved
creature-only Territory when a later black or green Dragon pip needs the
Territory. The focused Song → Karrthus drop and another Kolaghan drop are
repaired. A 6,000-game paired Release screen was play-identical and the
3,353-case unit suite passed. Source `e875cfffd` is default on with a `=0`
rollback lever. The fresh PGO gate 52,000–56,999 was oracle-on/off
identical and found 115 issues: 6 enum gaps, 79 drops, 26 order drops, and
4 oracle gaps. The next block is 57,000–61,999. See
`docs/design/dragonstorm-storage-before-territory-2026-11-16.md`.

Prevent Damage's harmful nonexact pain-floor payer now retains the initial
payment when the cheaper payment strands a declared line and the initial
payment survives. The focused five drops at seed 30,058,249 are gone; a
5,000-pair held-out screen was play-identical, all 3,354 unit cases passed,
and strict viewer replay had no drift across 864 references. The fresh PGO
gate 62,000–66,999 was oracle-on/off identical with 404 issues: four drops
in one new post-breakpoint Dina → Earthquake seed, 26 order drops, 171 oracle
gaps, ten unmodeled, 15 capped, and 178 decision-budget flags from one
decision. The next block is 67,000–71,999. See
`docs/design/prevent-damage-nonexact-pain-line-2026-11-16.md`.

The next Prevent Damage drop cluster, seed 30,064,758, has two visible
Earthquakes after a Dina breakpoint. `MTG_PD_MULTI_QUAKE_HOLD` now preserves
two red pips for Dina, then subtracts the current spell's red pip when paying
the first Earthquake. The recorded-board test pays all three spells. On
5,000 paired Release games, nine play traces changed and no win turn moved;
the optimized trial has the same play as the first trial with 2.0% extra
recorded game time and 0.06% fewer work units than the off arm. All 3,355
unit cases passed. Strict viewer replay had zero drift across 864 references;
the one changed regression row has zero paired win-turn changes. The default
is on and `=0` rolls back. The fresh PGO gate 67,000–71,999 was
oracle-on/off play-identical with zero APPLY-DROP, 12 APPLY-DROP-ORDER, and
59 ORACLE-GAP flags. The next block is 72,000–76,999. See
`docs/design/prevent-damage-multi-earthquake-hold-2026-11-16.md`.

Prevent Damage's low-life Earthquake cast-order fallback now moves only
visible Earthquakes earlier when the preferred order cannot pay a two- or
three-cast set. It clears all three focused order-drop signatures. A
5,000-pair Release A/B changed three plays and no win turns; the narrowed
default-on form has effectively flat work and recorded time. All 3,355
unit cases passed, strict viewer replay had zero drift across 864 references,
and a same-binary `=0` regression control showed zero win-turn differences.
The fresh PGO gate 72,000–76,999 was oracle-on/off play-identical with zero
APPLY-DROP, APPLY-DROP-ORDER, or ENUM-GAP flags. It found 135 ORACLE-GAP
and five UNMODELED flags, so the deck remains beta. The next block is
77,000–81,999. See
`docs/design/prevent-damage-payable-quake-order-2026-11-16.md`.
The next local oracle repair distinguishes old/new copies of a pain land during
scripted payments. Focused replays clear the 32 two-Vito flags in seed 30073845
and 11 Beseech/Vito flags in seed 30073663. Seed 30076504 remains a separate
batch-prepay self-lethal mismatch. The fresh PGO gate 77,000–81,999 was
oracle-on/off play-identical, with zero APPLY-DROP, APPLY-DROP-ORDER, or
ENUM-GAP, but 64 ORACLE-GAP, six UNMODELED, and five CAPPED flags. The next
block is 82,000–86,999. Most remaining gaps are joint prepay before
Manabarbs versus per-cast oracle timing. Windows CI run 43826425327 for the
old/new pain-land identity fix passed. The default-on audit-only batch-float
repair clears five focused seeds and changed no play output in a 100-game
paired cost check (+0.8% oracle time). The fresh 5,000-game PGO gate on
82,000–86,999 was oracle-on/off play-identical. It found 298 issues: three
continuation payment drops, 24 oracle gaps, 18 payment-table caps, ten
plan caps, and 243 unaudited-plan flags from one Pyrohemia decision. The
next block is 87,000–91,999. See
`docs/design/prevent-damage-batch-float-oracle-2026-11-16.md`.
The next Prevent Damage payment fix keeps a red source when a breakpoint
continuation pays Dina before a visible Spellshock. The broad hold worsened
two of 5,000 paired games; the narrowed Dina-only hold clears the focused
three drops and changed no play digests or win turns on those 5,000 games.
It is default on with `MTG_PD_SPELLSHOCK_HOLD=0` rollback. The fresh PGO
gate on 87,000–91,999 was play-identical and found zero lost actions or
enumeration gaps; eight oracle gaps and seven plan caps remain. The next
block is 92,000–96,999. The 3,355-case unit suite passed; strict
viewer replay found zero drift in 864 references; a same-binary off control
reproduced all nine deck regression-row `.wins` despite three existing
ground-truth mismatches. See
`docs/design/prevent-damage-spellshock-continuation-hold-2026-11-16.md`.

The scripted-payment boundary fix and subsequent terminal-outcome,
sacrifice-context, and payment-table oracle fixes are committed as
`059cec44f` and `5a7a4a9a1`. Their focused reproductions and full unit/viewer
checks passed. The fresh PGO gate on Prevent Damage games 97,000–101,999
passed all 5,000 games with zero mana-clean issues and byte-identical
oracle-on/off play. Prevent Damage is clean on this source. See
`docs/design/prevent-damage-scripted-payment-trigger-2026-11-16.md` and
`docs/design/prevent-damage-terminal-oracle-payment-cap-2026-11-16.md`.
Dragonstorm's 115-issue gate at 52,000–56,999 is the next active cluster;
the first order-drop repro is seed 30,054,701, turn four.

## Codex update (2026-11-15)

The Gods Loam/dredge question below is resolved. The user clarified that Gods
should dredge toward Mystic Retrieval for Replenish and use Life from the Loam
to recover lands when it can, drawing naturally at zero or one land with none
in hand. `MTG_GODS_RETRIEVAL_DREDGE` is integrated default on. The coupled
search worsens, but paired play with blind search and blind mulligan bottoming
favors strict dredging: 261 fewer total turns in 2,000 fresh
solo games and 69 fewer in 1,000 fresh two-headed games. A narrow three-land
Loam alternative was tested and dropped. The Gods-only ground truth was
accepted with the coupled regressions disclosed. See
`docs/design/gods-retrieval-dredge-codex-2026-11-15.md` for the measurements.
The fresh PGO mana-clean diagnostic for Gods, Prevent Damage, Shadowborn, and
Dragonstorm completed 311 of 312 oracle-on chunks (19,963 complete games).
All four decks have confirmed misses, so none could be marked clean on this
block. The remaining Gods chunk spent about 45 minutes inside one
breakpoint-continuation audit without returning to the priced-cap check; the
run was stopped to work on the identified misses. Its partial evidence is in
`build/scratch/mana_clean/codex-20261115-gods-pd-shadow-ds/`. The first
focused trial makes Reflecting Pools pay before their sole last-counter
Gemstone Mine anchor; it removes the Gods drops at seeds 30031096 and
30031276. A fresh 5,000-pair Gods check found three faster games, none
slower, and 0.16% fewer search units. The optimized default-on implementation
and targeted regression checks are complete; see
`docs/design/reflecting-pool-anchor-payment-2026-11-15.md`. A full fresh 5,000-game
gate is still owed after fixes. This update supersedes the older Gods decision
and pending-gate notes below.

The long audit is a cap polling gap: `manaoracle::AuditDecision` checks its
priced limit between plan audits, while a plan's `ApplyPlanDirect` can derive
breakpoint continuations for a long time. `EnumChargeScope` posts odometer
visits only at scope exit. An oracle-only in-derivation poll and an elapsed
wall backstop are implemented locally. The default-cap repro finished in
304.6 elapsed seconds, recording `WALL-BP-CAP` and all 650 unaudited plans as
`CAPPED-DECISION` (171.951 priced seconds, 301.254 audit wall seconds).
Either interruption reports `CAPPED-DECISION`, never a truncated clean
verdict. The outer on/off comparison was stopped; its completed oracle-on
chunks remain diagnostic evidence, not a passed gate.

The preliminary oracle-on misses by affected game are: Gods 4 `APPLY-DROP`,
10 `ORACLE-GAP`, 2 `CAPPED`, 1 `CAPPED-DECISION`, 1 `UNMODELED`; Prevent
Damage 1, 24, 15, 0, 1 in those classes; Dragonstorm 39 `APPLY-DROP`, 22
`APPLY-DROP-ORDER`, 2 `ENUM-GAP`, 4 `ORACLE-GAP`, 3 `CAPPED`, 1
`CAPPED-DECISION`, 1 `UNMODELED`; Shadowborn 3 `APPLY-DROP`, 1
`APPLY-DROP-ORDER`, 5 `ENUM-GAP`. These are distinct games per class, not
event counts; some classes overlap in one game. The incomplete Gods chunk
may add misses.

The handoff's item H (non-clairvoyant re-decision after an unknown draw) was
already implemented and validated in the shared source; see the Item H entry
in `docs/design/mana-clean-pass-checkpoint.md`. The user also chose to drop
Linux CI because agents run Linux, keep the Windows check, and continue queue
work while that check runs asynchronously.

The Reflecting Pool/Mine anchor payer is committed as `05ee95805`: both
targeted Gods tiers were accepted after a per-game audit (zero slower games,
only three depth-zero play-digest changes, all still losses); all 1,231
ground-truth logs are consistent. The earlier pushed Gods/Boars commit
`6cb9a7c85` passed the Windows-only CI run 43726820576. The Shadowborn
five-seed `ENUM-GAP` cluster is now identified as hidden Dark Ritual copies
cast through Thrumming Stone, with physical ripple provenance retained across
a later Diabolic Intent shuffle. Focused oracle reproductions report five
`HIDDEN-INFO`, zero `ENUM-GAP`; the other Shadowborn drops remain. See
`docs/design/shadowborn-ripple-hidden-mana-2026-11-15.md`.

The ten Gods `ORACLE-GAP` games were a payment-table coverage hole: the engine
prepaid ordinary lands across Study/Looting then Magus, while the oracle
stopped after the first one-mana payment. An audit-only extension of up to two
extra taps, bounded by the plan's later named bill, removed all 30 gap events
on those games. The 100-game unrelated Gods cost probe showed +11.5% oracle
applies and +3.4% oracle time, with identical play. See
`docs/design/gods-oracle-line-surplus-2026-11-15.md`. The fresh PGO gate
`codex-gods-post-surplus-20261115` completed 5,000 paired oracle-on/off games
with byte-identical play in all 157 chunks. It found zero `ORACLE-GAP` flags,
but failed overall: two `APPLY-DROP` games, six `CAPPED`, four
`CAPPED-DECISION`, and one `UNMODELED` (13 distinct games). The existing
`MTG_SCARCE_COLOR_MASK=1` clears both dropped-action repros, but one of 1,000
paired Gods games loses a turn-seven win even at unlimited depth-eight budget;
the mask remains off. The Gods visible-state draw follow-up is complete: the
broad four-land rule was slightly worse in 1,000 blind pairs, while a narrower
four-Gods/no-Replenish-in-graveyard rule was essentially tied across 2,000
held-out blind pairs (five faster, five slower, one fewer win). Neither is
adopted; the isolated worktree was removed after the reference safety check.
The `UNMODELED` seed 30034901 was a sibling-order audit cap: raising its
initial limit to 20,000 resolved it, and a targeted on-demand sibling retry
is implemented locally. Its focused default-setting replay reports zero
issues; a fresh full gate is still owed.

The new sibling retry passed 3,308 unit tests, its focused Gods replay, and
the strict viewer check (854 references, zero drift). A full pooled PGO smoke
and regression run finished 555 cells and 558 scenarios, but 71 cells differ
from committed ground truth across unrelated decks. Three representative
failed cells (Mill smoke d3, White Knights smoke d3, Creature Giving
regression d3) replayed byte-identically on the parent source, current Release,
and current PGO binaries, while all three differ from the committed GT files.
That broad baseline drift predates the audit-only retry and was not accepted.
The 1,231 GT files remain internally consistent. Focused comparison data are
in `build/scratch/gods_pre_retry_replay/`; the full run is
`build/scratch/gods_alt_order_retry_pgo_regression.out`.

The six Gods `CAPPED` games were replayed with 20,000 and 100,000 retry caps
on the same PGO binary. Raising the on-demand retry ceiling cleared all 46
flags, added 28.4 oracle seconds across those six games, and left all six play
logs byte-identical. The 100,000 cap is now the local default; it still needs
build/unit validation and a fresh 5,000-game gate. The four
`CAPPED-DECISION` games and two `APPLY-DROP` games remain.

After the 3,308-test unit pass and the fresh PGO build, a new Gods block
(`codex-gods-retry100k-20261115`, seeds 35000–39999) started on source tree
`4b4e9a66674758f4c06ea1c6c5a248ba27404699`. It was stopped after 16,435
decision audits because the shared branch advanced to a Red Elementals source
with broad engine changes, and the partial block had already found issues:
six `APPLY-DROP` events in seeds 30035319 and 30035713, two `CAPPED` events
in seed 30035423 (the 100,000 retry still caps), and 2,553
`CAPPED-DECISION` events in seeds 30035211, 30035417, and 30035829.
Raw partial evidence is in
`build/scratch/mana_clean/codex-gods-retry100k-20261115/on.0.err`.
This is a diagnostic, not a 5,000-game gate. The seven local commits were
rebased onto `c6fd809f5`; the integrated source tree is
`5c89219595fc9e589c127661d37ee5da95ee6968`. A full integrated-source
Release build, tests, and then a fresh PGO gate are owed. The older
`test/mana_clean.json` record remains the last completed official gate.

2026-11-15 continuation: integrated Release passes 3,350 unit cases. A
colour-specific Reflecting Pool anchor fix (commit `74082b96e`) clears all
five new `APPLY-DROP` events in Gods seed 30035319. Its 1,000-pair Gods trial
had one faster win, zero slower, and four play-digest changes. Combined
Gods/Gods2HG smoke and regression had zero slower games; one two-headed
play line changed at the same T6 and was accepted after a flag-off control
reproduced the prior digest. See
`docs/design/reflecting-pool-anchor-payment-2026-11-15.md`. The three older
drop games still have four `APPLY-DROP` events. A focused replay at a
200,000-node oracle retry cleared both `CAPPED` events in seed 30035423,
with identical play and 2.6 extra batch seconds; the default is now 200,000.
The three `CAPPED-DECISION` seeds exhaust their budget in gate-observation
enumeration before any rejected plan is checked. A fresh PGO build and
integrated Gods gate are next.

2026-11-15 later continuation: the narrower counter-land scarce-colour hold
cleared the four remaining focused `APPLY-DROP` events but failed held-out
adoption: 5,000 paired Gods games had zero faster, one slower, three other
same-turn play changes, and +0.052% search units. It remains default off;
see `docs/design/gods-counter-scarce-mask-2026-11-15.md`. The three capped
decision seeds were replayed with a 900-second audit budget. All completed;
their expensive decisions used 205, 264, and 560 priced seconds, with no
remaining coverage gap. The default bounded decision budget is raised from
300 to 600 seconds, pending the fresh full gate; see
`docs/design/gods-oracle-decision-cap-2026-11-15.md`.

## The program (USER, standing)
Run every deck through `python3 scripts/mana_clean.py run --games 5000` (always pass `--games 5000`), fix every issue,
mark each deck. A deck leaves beta only after a clean analysis of >= 5,000 games on a FRESH seed block run after its
fixes (`python3 scripts/mana_clean.py status` lists each deck's next block). Fixes are mechanism-level, LOSSLESS
(everything findable at unlimited budget) and NO-GREEDY. Work items serially, one engine job at a time
(`MTG_RSS_CAP_GB=40`). Adoption bar for a play change: units, repros, `bash test/regression.sh --smoke` and
`--regression` (note the `--` flag form), a pooled held-out A/B with each deck's net <= 0 and every slower game
recovering at `--depth 8 --budget-ms 0`, `python3 test/check_gt_logs.py`. No overnight tier. Gates use `./build.sh pgo`.

## Branch state at handoff
- `phase-1-2-deck-analyzer`: last pushed by this session at ~08:00 UTC (246e34da6..78ba73d2d); local is ahead by doc
  commits only (item E checkpoint bullet, this file). The next shared push by this session's rhythm would be >= 12:00 UTC.
- CI for that push: run 43573881414 was cancelled by another session's push; follow-up run 43578742559 includes it:
  Windows build green; ubuntu + determinism parity had not landed at handoff. Check `gh run view 43578742559`.
- `wip/item-W-merge` (local): item W WIP (see below). `wip/gods-gy-option-demoted` 847943ebf (pushed): item E.

## CI: ubuntu hang in the viewer line-build check (check FIRST)
Runs 43573881414 and 43578742559 (both contain this chain's push 246e34da6..78ba73d2d) were cancelled by newer pushes,
and in the second the ubuntu job sat in `node test/viewer_linebuild_check.js` for 3 h 12 m after printing all 22
sub-checks (0 FAIL, 835 references). The last green run 43564679055 (before the push) ran that step in 1 s on the same
runner image; locally on the current tree the script exits 0 in 4 s (`process.exit(main())`, no child processes), and
the pushed range touches none of its inputs. So determinism parity has NOT run for this code yet. Newest run
43596869487 was being watched (log `build/scratch/claude1110/ci-push3c.log`). If ubuntu hangs in that step again it
is a repeatable Linux CI hang to root-cause. Open USER question: add `timeout-minutes` to that workflow step? (The
repo's no-timeout rule covers commands agents run; a CI step limit is the USER's call -- not changed.)

## RUNNING / IN PROGRESS at handoff
**Item W -- wild-mana prepay is lossy in places** (from the Spirits session's
`docs/design/handoff-mana-overnight-2026-11-14.md`; its status section is to be appended when W resolves).
Handoff: `logs/mana-clean-chain/W-handoff.md` (section #2 is the latest). **Current WIP: `wip/item-W-defer`
5571c1edc** (`wip/item-W-merge` points at the same commit). The earlier merge approach was DROPPED (it floated a
two-colour wild unit, against the USER ruling below, and still lost 3+-colour lines). Replaced by
`MTG_PREPAY_WILD_DEFER` (default ON): a generic pip gives back the coloured unit it took when a later pip needs that
colour and takes another unit from the float; give-backs are dropped if the float changed since the payment (CR
106.1b safe); the pool always shows real colours. GameState 1496 -> 1504 bytes. Builds clean; NOTHING run yet (3 new
unit tests, suite, repros, tiers, A/B all owed). Temporary `MTG_W_PPDBG` prints in TurnSolver.cpp must be removed.
`build/Release` holds the WIP binary: rebuild on the shared branch before any tier run. Next steps in order: run
units + repros (dragons gi363 may still fail on the 11-vs-7 pricing below); apply the same give-back in the
single-cast payer when a two-mana land (Azorius Chancery) floats its leftover (item 3); remove debug prints; validate;
correct the checkpoint doc's KNOWN GAPS claim; append RESOLVED/PARKED to the Spirits handoff doc. Findings:
- Item 1 dragons overnight d5 s4004 gi363 (seed 4367): the generic drain commits {R} to Urza's Incubator's {3}
  where the legal assignment puts Gruul Turf's {G} there, so Lathliss #2 is refused. Round 2's "KNOWN GAPS
  (permissive, never lossy)" claim is WRONG: committing a generic pip's colour at spend loses legal re-assignments.
  The merge (two units -> one "either colour" unit) fires but the drain spends it before {C}, so it does not yet help.
  Also the 3-spell bill is priced 11 vs 7 available (second Incubator's discount not counted). Alternative to
  consider: defer generic pips' source choice to the end of the bill (exact assignment over the units).
- Item 2 niv gi380: RESOLVED as a CR 106.1b correctness cost (old line paid {R} with {U}); same for niv gi522/gi244.
- Item 3 bruna overnight d5 s7007 gi432 (seed 7439) + niv held-out gi815: `MTG_PAY_HAND_GENERIC` spends Azorius
  Chancery's {U} on Sol Ring's {1} because the hand-demand count cannot see Bruna wished from the sideboard
  (Glittering Wish). Not fixed.
- Item 4 niv held-out seed 671138 gi1138 and edf gi549: legal old lines lost; retest once the merge works.
- USER ruling to honour: floating wild mana "shouldn't show up in anything" -- a merged unit must render as concrete
  taps in the viewer/logs.
- USER: "Draws Diverge is not an acceptable explanation, since at d8 b0 we should still be able to find the line."
- After W lands, the Spirits session rebaselines the overnight tier.

## USER DECISIONS open (surface them; never settle them)
1. **Item E, Gods Loam** (`wip/gods-gy-option-demoted` 847943ebf, lever `MTG_BP_NEW_ONLY_GY_TARGETS`): an old Life
   from the Loam counts as new at a breakpoint when the graveyard gained a land card. 12/13 ENUM-GAP repros fixed;
   held-out gods 5000/arm net +3 (2 slower, both recover at d8 b0; 1 win lost at the shipped budget). Recommended:
   adopt. If adopted: cherry-pick, smoke, regression, PGO, gate gods on 30000-34999.
2. **Culling the Weak victim fallback** (Shadowborn/Martyr area): sacrifice the battlefield Reaper when hand-Apostle-
   first does not pay (same shape as the Ritual rule). Not implemented.
- Answered 2026-11-14: keep tables are regenerated ONLY by the artifact queue when the USER runs it (USER: "Keep
  tables should only be regenerated by the artifact queue when I run that.") -- the Martyr table is not regenerated
  by this chain.

## TODO queue (serial, in this order, after W)
- **C2 r3 Pirates residual** (pirates 12 issues on 79000-83999; next block 84000-88999): hand-demand keep holds a
  creature-only land for a hand creature the line does not cast this turn, stranding Bolt's {R} (s30080205,
  s30079131, s30079307); s30083181 unanalysed; Staunch Crewmate dig casts an undeclared creature (s30070533);
  Fiery Islet pain tie-break vs Dire Fleet Captain (s30077738). `logs/mana-clean-chain/C2-handoff.md`.
- **H** NC re-decide after a draw. USER: "Yes, it might be painful for cost, but non-clairvoyant lookahead of all types
  should re-evaluate after a draw UNLESS we already knew what the card was from previous actions."
- **G+J** Myriad Landscape always fetches 2 (drop `land_search_allow_partial`; USER: "Why would we not search for 2
  lands with Myriad. Anything else is silly and only worthwhile for clairvoyant cases."); Mind Stone sac-draw offered
  iff (extra-turn spells in hand == 1 AND total mana >= 8 counting the Stone) OR (0 extra turns AND mana retained
  after the sac >= 5; test 5 vs 6, report). Re-size the ET oracle cost, then the full ET gate (long run, 2-hourly
  status timer); USER: "we may need to raise the per-decision limit ... I think its best to actually have all decks
  make it through analysis."
- **K** Jumbo: (1) GSZ-fetched untapper before Overgrowth only when the ranked order fails and the swap pays;
  (2) Overgrowth before a one-mana Aura on any land when the ranked order fails and the move pays -- adopt each only
  if A/B net <= 0, no perf cost, fixes lines; (3) amend `JumboOvergrowthAtBreakpoint` (DecisionProviders.cpp):
  breakpoint = outside-only mana O == 3 OR the stacked land's own S == 3, never mixed O+S == 3.
- **L** Hydras. USER: "The doublers out is better, but not if we can't afford to do it that way." -> fetch before the
  last doubler ONLY when the ranked order cannot pay and the moved order can; A/B.
- **N** Fungus cast order: all USER answers in `docs/design/fungus-cast-order-proposal-2026-11-13.md`; first re-check
  site-9 Burst pops on v3 (Q9), diagnose the count menu (Q16).
- **Item 10 PD leftovers**: ActLineHoldMask (s30027719), then PGO re-gate on 32000-36999 (`logs/mana-clean-chain/10-handoff.md`).
- **Dragonstorm re-measure** -- UNBLOCKED: the other session's Dragons processes (347826/347828/356311/356792) ended ~11:30 UTC 2026-11-14 (needs no USER question).
- **Shadowborn residual** (9 issues on 60000-64999; next block 65000-69999): parked floatseq class (Sire dropped
  after Diabolic Intent; needs a searched Lotus early-crack colour pin), ENUM-GAP s30061713 / s30062801.

## Report items still owed to the USER
- The wild-mana path's correctness costs (Cyclone/Hinata/Niv +1/+1/+3; Niv gi128/gi143 T4->T5) -- now amended by item W.
- The prepay-before-draw topic.

## Working rules this chain followed (all from the USER; see CLAUDE.md)
Shared-branch push at most once per 4 h per session, subagents never push it; references commit-only and
`scripts/check_references_safe.py` before every push; rebase never merge; never `timeout`; waiters on `kill -0 <pid>`
or DONE markers; agents hand off past ~250K context, normally ending 250-350K, never above 400K; never block on a
question (surface it, take the default, keep going); USER-owned decisions are surfaced, never settled.
