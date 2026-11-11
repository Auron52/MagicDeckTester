# Jumbo Cactuar analysis

Deck: `decks/Jumbo Cactuar/Jumbo Cactuar.cod` (60 cards, mono-green).
Started 2026-11-04. The baseline profile is shipped. The final horizon
tie-break check remains open; the same-run suite cost gate passed.

## Playing the deck

The primary kill is nine mana for entwined Tooth and Nail, fetching and
putting Surrak and Goreclaw plus Jumbo Cactuar onto the battlefield. Surrak
gives Cactuar haste and a counter, and its attack trigger deals lethal damage.
A Cactuar already in hand or reached by Zenith can instead use Chariot or
Greaves for haste; two creature tutors can fetch Surrak first, then Cactuar.
Finale for X=10 also grants team haste, but its twelve-mana cost makes it a
later route unless the Aura stack produces the extra mana.

The mana plan is to put land Auras on the highest-yield Forest and retap it
with one Arbor Elf or Voyaging Satyr. Zenith can fetch Arbor Elf when the
untapper is missing. A naturally drawn Satyr is usable, but in this all-Forest
goldfish Arbor Elf is the better low-mana tutor target. The proposed cleanup
policy in `docs/design/jumbo-cactuar-discard-policy.md` applies these
breakpoints and is still inactive.

The mulligan bucket ruling merges Utopia Sprawl and Wild Growth only. The
400-probe feasibility check found exactly 13 classes from 14 distinct mainboard
cards. Arbor Elf and Voyaging Satyr remained separate (distance 0.2675), as
their untap restrictions and costs differ. Discovery had rare depth-five
rollouts lasting 30–70 seconds, so the generation-time projection matters even
though the regression suite's average cost is within its gate.

### Proposed cast order for review

Lower ranks resolve first within a multi-card plan. Forest is a land drop, not
a spell; it is listed so every one of the 14 distinct mainboard cards has a
place. This order has no card-level ties.

| Order | Card | Copies | Rank | Reason |
| ---: | --- | ---: | ---: | --- |
| 1 | Forest | 19 | 0 | Play the land before spending mana. |
| 2 | Wild Growth | 4 | 10 | One-mana Aura can fund later casts. |
| 3 | Utopia Sprawl | 4 | 20 | Second one-mana Aura, with the color fixed to green. |
| 4 | Overgrowth | 4 | 30 | Larger Aura after its cheap enablers. |
| 5 | Lightning Greaves | 1 | 35 | Cast before a fresh untapper so its free equip can unlock mana. |
| 6 | Surrak and Goreclaw | 2 | 37 | Enter before a newly hasted mana untapper or Cactuar. |
| 7 | Arbor Elf | 4 | 40 | First mana untapper; existing haste equipment can attach immediately. |
| 8 | Voyaging Satyr | 4 | 50 | Second mana untapper; existing haste equipment can attach immediately. |
| 9 | Chariot of Victory | 4 | 60 | Cast after both untappers; its equip can still unlock mana for a later spell. |
| 10 | Nylea, God of the Hunt | 1 | 80 | Board support after mana setup. |
| 11 | Tooth and Nail | 4 | 90 | Fetch and put both kill pieces when entwined. |
| 12 | Green Sun's Zenith | 4 | 110 | Tutor; its actual target can move this action earlier. |
| 13 | Finale of Devastation | 1 | 120 | Tutor or twelve-mana haste finisher. |
| 14 | Jumbo Cactuar | 4 | 130 | Enter after haste setup. |

The tutor action's target refines its rank: Arbor Elf at 38, Surrak and
Goreclaw at 95, and Jumbo Cactuar at 125. Thus two Zeniths, or Zenith plus a
hand-cast Cactuar, still resolve Surrak first. The card-level table is a
proposed order for user review; the viewer repro and regression checks below
measure its effects.

### USER cast-order rulings (2026-11-11)

The table above was never reviewed until 2026-11-11. The USER's words, verbatim:

> If we are going to value GSZ and Finale based on the target, which I think is fair, then yes they should always
> go after the cheaper cast-from-hand option. I don't understand how Overgrowth ever goes before Wild Growth, I
> don't think it should. Casting chariot after the first Elf/Satyr might make sense, but it is relatively tricky to
> specify. If you want to try that, it would be okay. Also, Surrak and Goreclaw is risky in the 6th spot there. It
> seems like the kind of card you want to put earlier only if mana allows.

How they apply (implementation queued in the mana-clean chain, item 5h):
1. A Zenith/Finale ranked by its target goes AFTER a hand-cast copy of that target (Elf, Surrak, Cactuar alike;
   the current target ranks 38/95/125 put Elf and Cactuar tutors before the hand copy).
2. Overgrowth never goes before Wild Growth. Seed 30029950 T6 (Overgrowth-first pays Tooth and Nail) is therefore
   treated as a payment defect to find, not an order change. Whether the existing proven Overgrowth-before-Utopia
   Sprawl swap should also go is an open USER question.
3. Chariot after the first Elf/Satyr: optional, may be tried and measured. USER follow-up (verbatim): *"I can't
   think of a way that with only one elf/satyr that this worder would be wrong."* and *"It pretty much has to be
   multiples coming into play to need the chariot cast earlier."* So with one untapper in the line the current order
   (untapper, then Chariot) stands; only a line with two or more untappers entering puts Chariot after the FIRST.
4. Surrak and Goreclaw moves out of the 6th slot to a later default, and comes earlier only when mana allows,
   i.e. a proven swap (the earlier order pays every cast under OrderPaysSequentially), like the Aura swap.

### Ability activation order

The table below has one distinct priority per activation type. An activation
waits until its source and target are in play. Mana abilities fire only when
payment needs them. A haste equip onto a fresh mana untapper fires as soon as
both permanents are in play, before the next cast. This includes Chariot
already on the battlefield when Elf or Satyr enters, and Chariot cast after
Satyr. Other activations follow the casts. The early equip rule lets Greaves
attach to Arbor Elf, the Elf untap the enchanted Forest to pay for Satyr, then
Greaves move to Satyr if that second untap is useful. Moving Greaves is an equip activation;
it is not an extra cast.

| Order | Activation | Window | Reason |
| ---: | --- | --- | --- |
| 1 | Equip Lightning Greaves to a fresh Arbor Elf | Immediately after Elf enters | Unlock its Forest untap for the next payment. |
| 2 | Equip Chariot of Victory to a fresh Arbor Elf | As soon as Elf and Chariot are both in play | Do this when its one-mana equip cost is repaid by the untap. |
| 3 | Activate Arbor Elf to untap a Forest | During mana payment | Prefer the highest-yield enchanted Forest. |
| 4 | Equip Lightning Greaves to a fresh Voyaging Satyr | Immediately after Satyr enters | Move Greaves from a tapped Elf when Satyr's untap can pay for more. |
| 5 | Equip Chariot of Victory to a fresh Voyaging Satyr | As soon as Satyr and Chariot are both in play | Unlock Satyr when the net mana gain is useful. |
| 6 | Activate Voyaging Satyr to untap a land | During mana payment | Prefer the highest-yield enchanted land. |
| 7 | Activate Nylea, God of the Hunt to pump a creature | After casts, before Greaves goes onto that target | Greaves gives shroud and would prevent a later targeted pump. |
| 8 | Equip Chariot of Victory to an attacker | After casts, before Greaves on the same target | Grant haste without running into Greaves' shroud. |
| 9 | Equip Lightning Greaves to an attacker | After other targeted actions | Grant haste for combat. |

The two untap abilities are represented by the mana payer; the viewer does not
require a separate manual untap action. An equip remains a selectable action.
The focused viewer cases cover Greaves → Elf → Satyr and Greaves → Elf → Satyr
→ Nylea with both Greaves equips. Chariot cases cover a cast after Satyr and
an equip after either untapper enters while Chariot is already in play. All
Jumbo Cactuar scenarios and all 49 focused unit assertions pass with this
order.

## Coverage and card ledger

Stage 1 coverage found seven missing mainboard cards. The sideboard is unreachable
without a wish effect, so its extra Finale copies and Haunted Cloak are not
part of this goldfish. Existing Lightning Greaves, Arbor Elf, Wild Growth,
Utopia Sprawl, Overgrowth, Forest, and Green Sun's Zenith have full coverage;
their existing implementation notes still need final clause review.

| Card | Live Scryfall cost | Required work | Status |
| --- | --- | --- | --- |
| Chariot of Victory | `{3}` | Equipment equip `{1}`, haste and first-strike grants; trample has no blocker path | Integrated and verified |
| Voyaging Satyr | `{1}{G}` | Untap any target land, including an enchanted Forest; preserve land choice | Integrated and verified |
| Jumbo Cactuar | `{5}{G}{G}` | Attack trigger grants +9999/+0 until end of turn | Integrated and verified |
| Nylea, God of the Hunt | `{3}{G}` | Devotion to green, repeatable targeted +2/+2 activation, trample grant, indestructible | Integrated and verified |
| Surrak and Goreclaw | `{4}{G}{G}` | Another nontoken creature enter trigger adds a counter and haste | Integrated and verified |
| Tooth and Nail | `{5}{G}{G}` | Modal two-creature tutor or hand put, entwine `{2}`, ordered decisions | Integrated and verified |
| Finale of Devastation | `{X}{G}{G}` | Library/graveyard tutor, X threshold team pump and haste | Integrated and verified |

Live Oracle details that determine the spell models: Tooth and Nail's search
mode finds up to two creatures into hand; its put mode puts up to two creatures
from hand onto the battlefield; entwine `{2}` resolves both in that order.
Finale searches library and/or graveyard for one creature with mana value at
most X, puts it onto the battlefield, and gives all controlled creatures +X/+X
and haste until end of turn when X is at least ten. The pump still happens when
the search finds nothing. These are separate decision axes; an X choice limited
to a tutored creature's mana value would miss the finisher.

The user identified the intended Tooth entwine line: search for Jumbo Cactuar
and Surrak and Goreclaw, put both onto the battlefield, then Surrak grants
Cactuar a +1/+1 counter and haste. Cactuar attacks as a 10001-power creature
for a same-turn kill. The user wants the pair selection encoded as a
JumboCactuarProvider heuristic; the unpruned full-search and human-play paths
must still retain every legal pair. A directed unit test and scenario fixture
cover simultaneous entry, haste and the lethal attack; both passed in the
optimized Release build.
Additional directed checks cover Finale fetching from library and graveyard at
X=10, its graveyard-only no-shuffle whiff, Satyr's exact land target, and
Nylea's devotion-dependent creature status and pump target.

Chariot of Victory, Jumbo Cactuar, Voyaging Satyr, Surrak and Goreclaw, and
Nylea, God of the Hunt now have card data and engine paths integrated. Chariot's
first strike reaches the shared combat oracle; Cactuar's fixed +9999/+0 fires
at declare attackers. Surrak's watcher modifies a nontoken entrant. Nylea's
devotion gate reuses the God implementation and its targeted pump has searched
target and activation-count choices. Satyr uses the Aura-aware Forest untap
model in autonomous play and an explicit target-land action in human play.
Tooth and Finale are integrated with searched mode, X, zone, and creature picks.
JumboCactuarProvider narrows autonomous entwined Tooth tuples to Cactuar and
Surrak when both are available; human play and `MTG_UNPRUNED` bypass it. All
seven new card paths compile in the optimized Release build; focused runtime
checks passed for the Tooth/Surrak/Cactuar line, Finale, Satyr, and Nylea.
Finale also exposes a graveyard-only empty search when no eligible graveyard
creature exists; that branch preserves library order and still gives the
X-at-least-ten team pump.

The user also proposed concentrating land Auras on one Forest so Arbor Elf and
Voyaging Satyr can untap the entire stack. `JumboCactuarProvider` ranks the
Forest with the highest current mana yield first, while retaining up to four
untapped Forests as alternatives. Four are needed because Overgrowth costs
three mana: payment may tap three Forests before its bonus is available, and
the fourth can fund a follow-up spell. A generic class fold had erased the
provider's second same-name Forest, so it now leaves provider-proposed host
sets intact. Human play retains every legal host. The final-build paired
`MTG_UNPRUNE=landaurahost` comparison matched all 600 win turns across two
150-game seed blocks at d3/b10 and d5/b20 (zero slower/faster at either depth).
Only two play digests changed, both still winning on turn four: the stacked
arm played a land Aura earlier in each. The stacked arm used 1,275,734
core-ms versus 1,285,544 for the full-host arm (0.8% less). The earlier
pre-breakpoint comparison had shown a larger 14.5% cost reduction; the final
result supports safe narrowing but little speed gain. The first, one-host
proposal lost 11 turns at d5;
two-host and four-host repros exposed the payment-order issue before the final
A/B passed. This remains deck-specific: a generic rule must account for
untappers, same-turn payment, land restrictions, and other host abilities.
For the untap itself, the shared Arbor Elf mana model ranks eligible enchanted
Forests by the full Aura bonus and chooses the highest yielding class first;
Voyaging Satyr uses the same model in autonomous play. A focused unit test
checks both against a one-Aura and a two-Aura Forest. Satyr's literal human
ability still exposes every legal land target.

The single-creature tutor proposal narrows Green Sun's Zenith and Finale to
Jumbo Cactuar when its mana is reachable, Arbor Elf when mana is short, and
Surrak and Goreclaw when Cactuar is just out of reach. It retains both Surrak
and Cactuar when two tutors are in hand, or Surrak when Cactuar is already in
hand, so Surrak can enter first and grant the later Cactuar haste. Directed
turn-four scenarios cover two Zeniths and Zenith followed by a hand-cast
Cactuar.
The final-build `MTG_UNPRUNE=tutor` A/B covered 600 paired games across two
150-game seed blocks at d3/b10 and d5/b20. Narrowing was faster in three
games at each depth and slower in one d3 game (seed 3105, game index 102:
turn four versus turn three). That slower game was already diagnosed on the
earlier build: the narrowed 10 ms search passed turn one's main phase without
playing Forest plus Arbor Elf, whereas the full arm played them. At d3 with
no budget and at d5/b20 the narrowed arm also wins on turn three. This is a
search-budget line shift, not an excluded tutor target. The narrowed arm used
1,920,911 core-ms versus 2,636,906 for the full arm (27.2% less). The one
slower budgeted game remains disclosed; the net result is three win-turns
better on the final build.

An independent Claude-play check at seed 73001 found a legal turn-three kill:
Overgrowth on the stacked Forest, then Finale with X=10 for Jumbo Cactuar.
The shipped d5/b20 search took turn four because Finale's initial action fan
stopped at the pre-Overgrowth X bound. Handing the post-Overgrowth board to the
same search yielded turn three. The proposed fix extends Finale's X fan by
the Auras and ready land untappers in the hand/board, and extends both subset
mana upper bounds by the same possible Aura retaps. Exact sequential payment
remains responsible for legality. A directed scenario and repeated seed replay
will determine whether this closes the search gap.
The final d5/b20 replay wins on turn three. The independent sweep also exposed
two human-play mana gaps: seed 73004 needed Satyr to untap a stacked Forest
before a Cactuar cast, and seed 73006 needed Satyr's extra Forest tap alongside
Arbor Elf to cast Surrak. Satyr and Elf now participate in mana payment
automatically, including the full Aura yield of the chosen land. The viewer
does not require a literal untap activation. Directed scenarios check both
casts and the later attack. Greaves can haste either untapper immediately
after it enters; a second equip can move Greaves from Elf to Satyr in the same
plan. The exact Greaves → Elf → Satyr → Nylea line needs both untaps and is
checked as a separate viewer scenario.

The sweep's seed 73009 initially looked like another tutor gap: full-search
GSZ for Voyaging Satyr won on turn four, while the narrowed provider won on
turn five. The user correctly pointed out that Arbor Elf is strictly the
better untapper for this mono-Forest goldfish and the deck has four copies.
The human-play menu offers the same turn-two GSZ line for Arbor Elf (X=1)
as for Satyr (X=2). Replaying the Elf line changes the GSZ shuffle: the
turn-four draw is Voyaging Satyr instead of Tooth and Nail. The Satyr line's
turn-four win therefore exploits the fixed-seed shuffle, not a better
mana plan. The provider continues to omit Satyr from single-creature tutor
targets at low mana; a focused test locks that rule in. The user approved
ignoring this clairvoyant difference on 2026-11-05.

The final 15-seed Claude-play sweep (73001–73015, depth 5/budget 20
benchmark) found and resolved three real gaps: Finale's post-Overgrowth
X=10 line (73001), the human Satyr untap funding later casts (73004/73006),
and the human GSZ X ceiling with both Arbor Elf and Satyr available (73010).
For 73010, the repaired turn-three menu offers GSZ X=6 for Surrak; after
choosing Satyr's enchanted Forest untap first, the cast pays and Surrak
enters. Seed 73003's reported mulligan mismatch and truncated menu were
replayed against complete JSON and dismissed. Seeds 73004, 73011 and 73013
also reported composite plans with the cast listed before an untap and a
subsequent dropped cast; selecting the untap first or casting in the next
main-phase frame executes the legal line. Seed 73009's apparent faster
Satyr tutor line is the shuffle difference above. No legality or state
transition flag remains unresolved.

## Claude-play sweep

commit: `12a476cda0df7778b3656adc24b5c2d91692e956` plus this working tree's
uncommitted Jumbo Cactuar changes.
seeds: 73001–73015.
games: 15, using the Claude-driven play oracle against the depth-5/budget-20
search; all reported differences were replayed and classified above.
flags: 0 unresolved.

The later post-GSZ Chariot continuation fix was validated with a directed
scenario and the searched-depth smoke games; it did not alter card legality.

A separate smoke outlier (seed 1050, game index 49) exposed a missed same-turn
GSZ-to-Chariot kill. Zenith put Cactuar onto the battlefield on turn six, but
the original action set had been built before Cactuar existed and could not
move either Chariot to it until the next turn. A provider-scoped post-creature
tutor breakpoint now re-enumerates the remaining main phase when Chariot or
Greaves is controlled or in hand. The directed
`jumbo_cactuar_gsz_chariot_same_main` scenario fails on the old build and
passes on the repaired build with a turn-six win. A second scenario covers
Zenith into Cactuar followed by a Chariot cast and equip from hand. The user
confirmed this is a common win line.

The final smoke check after widening that breakpoint to an Equipment still in
hand had no searched-depth slowdowns and improved two depth-five games by one
turn each. Greedy depth zero improved by nine total win-turns over 1,000 games,
with 13 faster and two slower games. The larger slowdown (seed 1249, T5 to
T8) followed three early Zenith casts and a different shuffle/draw; the other
(seed 1963, T3 to T4) equipped Greaves to Cactuar and then moved it back to
Arbor Elf before combat. The depth-five search keeps the lethal haste line.
This greedy Equipment move remains a known depth-zero limitation, rather than
a searched-depth legality gap.
The accepted final smoke row averages are 5.7890 turns at depth zero
(1,000 games), 4.4600 at depth three (50 games), and 4.3200 at depth five
(175 games). All 402 directed scenarios and all 2,447 unit cases pass on
the final build.
The accepted regression rows average 5.7700 turns at depth zero (1,000
games), 4.3500 at depth three (100 games), and 4.3840 / 4.3060 at depth
five in the two 500-game seed blocks. The unbudgeted depth-three and
depth-five 20-game checks both average 4.3000. Strict viewer replay and the
per-game audit passed with no searched-depth slowdown against existing
ground truth.

Viewer choices expected: Chariot's equip host; Satyr's target land; Nylea's
repeatable pump target and count; Tooth's single entwine prompt (yes, search
only, or put only) and both two-card picks; no separate X prompt for Tooth;
Finale's X and target zone/card. Cactuar and Surrak have mandatory triggers with
no choice. The static viewer audit passes for all 14 matched mainboard card
definitions (`scripts/audit_viewer_decisions.py ... --no-sweep`, 2026-11-04):
expected `choose_color`, `sac_tutor`, and `x_value`; no unmodeled Oracle choice
phrase. Main-phase actions are classified separately by that audit.

## Verification ledger

Coverage (14/14 full), baseline profile, all three regression tiers with
accepted ground truth, mismatch harnesses, multi-depth sanity, the Claude-play
sweep, the full viewer decision sweep, and play-invariant checks passed.
`verify_deck.py --no-network` passes all active gates. Its two skipped gates
are the intentionally disabled network cost request (audited separately)
and the clause ledger covered by the coverage/oracle checks. No mulligan
table or learned value leaf is part of this analysis. The final horizon
tie-break check remains open. The
final Aura-host A/B preserved all 600 paired win turns.
Lightweight checks passed: play line-builder syntax, viewer-audit Python
compilation, card and scenario JSON parsing, and `git diff --check`.

The optimized Release build passed. The complete unit suite passed (2,447
cases, 3,568,723 assertions), including simultaneous Tooth entwine plus
Surrak haste, the provider's missing-piece tuple, Finale's graveyard-only
whiff, Satyr's chosen land, and Nylea's devotion gate and pump. All 402
directed scenarios passed, including the two Zenith-to-Chariot continuations.
The offline field audit passed (676 checked), as did the live cost audit
(529/529) and provider-routing audit (`JumboCactuarProvider`).
The suite's same-run cost gate passed: Jumbo Cactuar 4,672.268 core-ms/game
versus the pinned Hinata yardstick 5,208.293 core-ms/game (0.90x; limit 3x).
The later same-run suite gate measured 2,645.229 ms/game for Jumbo versus
3,488.053 for Hinata (0.76x; limit 3x).
On the rebased tree, the final same-run gate measured 3,391.549 ms/game for
Jumbo versus 5,680.773 for Hinata (0.60x; limit 3x). All 757 saved viewer
references replayed with zero play drift, enumeration gaps, or contract failures.
Targeted `--claude-play` probes surfaced Tooth's separate search/put modes and
library/hand `sac_tutor` frames on the real deck (seed 1). Small synthetic
decks surfaced Finale's `x_value`, library-zone `sac_tutor`, and conditional
`attack_mode` for an empty graveyard-only search. A second synthetic deck
discarded Elvish Mystic, then cast Finale for X=1: its `sac_tutor` offered
16 library copies and the graveyard copy with distinct zone labels; selecting
the graveyard copy prompted `attack_mode` to search the library or skip the
shuffle. The graveyard-only choice put that Mystic onto the battlefield.
The broad Stage 5h sweep passed. The verifier's mismatch harness found no
nonconvergence or full-depth divergence across two 60-game seed blocks, and
the play-invariant sweep completed eight games / 198 decisions with no hard
flags or advisories. Its harness needed to recognize the already emitted
`choose_color` and Finale's optional `attack_mode` decisions; that harness
fix did not change simulator behavior.

Stage 5i's discard evidence saw zero real cleanup sheds in 400 games, but
1,913 rollout sheds, all with fewer than four lands out. Its verdict was
`DISCARD_UNLABELLED`, so a searched real-discard trace cannot judge the
rollout ranking. The paired `MTG_SHED_WORST=1` bound used 150 games per arm
in each of two cells, d3/b10 (seed 930100) and d5/b20 (seed 930200), in one
24-worker batch. Default and worst-ranked cleanup sheds had identical average
win turns (4.2533 at d3 and 4.3200 at d5), with zero faster games, zero
slower games, zero changed play digests, and no unpaired games in either cell.
The Jumbo provider does not narrow cleanup candidates, so the inverse-ranking
lever had a full candidate list. No production discard heuristic was changed.
The quota-first proposal is in `docs/design/jumbo-cactuar-discard-policy.md`.
After user review it now uses one shared five- or six-card mana bucket across
board and hand, keeps only the next turn's land except for two at end of turn
one, prefers Auras with one untapper, and lets Zenith fetch Arbor Elf to fill
that untapper role. It protects a reachable entwined Tooth and Nail line at
nine mana; Finale X=10 at twelve mana gets a slot only when its extra mana is
plausible. The draft is inactive pending review of the revised proposal.

## Saved reference check

The ten hand-played games in `references/Jumbo_Cactuar/` all replay by intent
through the current plan menu, reaching their recorded win turns with zero
enumeration gaps, board drift, or contract failures. With each reference's
mulligan count and bottomed cards forced, the shipped search wins on the same
turn in nine games and one turn earlier in seed 8 / game index 7. Human mean
win turn is 4.3; search mean is 4.2, with no shortfalls or hand mismatches.
This confirms the recorded lines remain executable and the search reaches at
least their win turns on the same starting hands; it does not claim the search
chooses the exact same sequence of actions.

## Mulligan profile and overnight baseline

The exhaustive `fast` profile uses K=13, merging Utopia Sprawl with Wild Growth.
The measured generation setting is d1/b1 (rank correlation 0.9955 against
d5/b20 play, above the 0.990 floor), with R=30. The first-version validation
accepted both keep (−0.323250 turns against static) and confounded blind
bottoming (−0.100167 turns against lookahead). The full overnight Jumbo rows
were expanded to four 1,000-game d3/b20 seeds and four 500-game d5/b40 seeds,
baselined with the table absent, then accepted with the table present. The
adoption attribution found 7,640 changed mulligan counts, 751 changed bottoms,
22 bottom-order-only digests, and four same-hand cases matching table-off play
when lookahead bottoming was restored.

The complete overnight suite was then rebaselined: 118 rows matched, 424
changed, and 11 new Shadowborn rows were added. Searched games improved 3,953
times versus 1,889 slowdowns. Angels (+0.0064 turns over 6,065 searched games),
Angels 2HG (+0.0150 over 600), and Bruna (+0.0122 over 2,131) remain visible
follow-ups in the accepted ground-truth note.

## Value-leaf generation and play setting

The value-leaf run used the committed profile at `16e2f6d2f` and collected
10,263 searched positions from 2,500 games. Its matrix completed 52/52 cells
at 400 games each, with no condemned cells. The trained model's held-out RMSE
was 0.5821. The model is retained as `Jumbo Cactuar.value.DISABLED.json`:
although it passed the first leaf check, the full shape comparison selected a
leafless final-depth single pass. That shape improved the paired mean by 0.0030
turns and used 0.941 times the search units of the previous live setting over
8,000 paired games.

The subsequent depth and budget grid selected d6/b20 for the live sidecar. It
matched the d5/b20 baseline's win turns while using 0.994 times its search
units over 8,000 paired games. The escalation-skip stage found no ladder
escalations and made no change. Jumbo smoke and regression ground truth were
re-accepted after the setting change; the full overnight baseline above
predates this play-setting adoption and will be revisited at the next periodic
overnight run.

## Open model questions

Trample on Chariot, Nylea, and Surrak, and Nylea's indestructible, are inert
against the simulator's passive opponent: it never blocks or destroys creatures,
and this deck has no self-destruction effect. The user approved recording these
clauses as inert on 2026-11-04. The implementation retains printed keywords.

## Approved deferrals

- Trample on Chariot of Victory, Nylea, God of the Hunt, and Surrak and
  Goreclaw: no blockers exist in the goldfish combat model.
- Indestructible on Nylea, God of the Hunt: no reachable destruction effect
  can act on it in this deck's goldfish.
