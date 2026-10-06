# A screen cannot be stratified on a win turn, and what it would take to stratify it honestly (2026-10-06)

Status: OPEN (the fix is unbuilt). Recorded because the invalid method is tempting, cheap to run,
produces large and plausible t-values, and was caught only by a check that is worth reusing.

## The question that motivated it

During the KittyEquipment v2 card-swap screens the user asked a conditional question: *does this card
earn its slot in the games where Puresteel Paladin never arrives?* Several cards in that deck exist
for a case the aggregate hides -- Sigarda's Aid and Ardenn are free-equip insurance against the
engine card being removed -- so the mean over all games is the wrong statistic for them.

`scripts/deck_compare.py` emits one `[win] job=<arm> gi=<n> wt=<turn>` line per game, so the
tempting move is to stratify the paired delta on the win turn: a late base win is a game where the
good start never happened.

## Why that is invalid

**Selecting games on an outcome and then measuring that same outcome's difference is circular.** The
games where base won unusually fast are the games where base's draw was unusually good; an arm
playing a slightly different 60 on that shuffle regresses toward the mean, so it looks slower. The
games where base won late produce the mirror. The size of the artifact is set by the variance of the
per-game difference, which is large here (6-15% of games change outcome at all), so it swamps the
effects under study.

Measured on the round-1 snapshot, all seven candidate arms flipped sign the same way -- positive in
the fast stratum, negative in the slow one -- which is already the tell, because arms with large
POSITIVE overall deltas cannot all be secretly good in slow games.

**The decisive check: stratify the SAME arm on the OTHER side's win turn.** If the conditional effect
were real the two readings would agree. For `gauntlets4_net3` (16,000 games):

| stratified on | base wt <= 3 | base wt == 4 | base wt >= 5 |
|---|---|---|---|
| **base's** win turn | +0.0463 (t +10.1) | -0.0470 (t -11.2) | -0.1177 (t -12.2) |
| **the arm's** win turn | -0.0558 (t -15.4) | +0.0010 (t +0.2) | +0.0494 (t +4.2) |

An exact mirror image. The sign is a property of which side was selected on, not of the cards. The
only honest number is the unstratified one.

Keep this check: **any stratification of a paired screen must be run both ways, and must agree.**
It is two lines of code and it refutes the whole family of outcome-conditioned reads.

## What would make a conditional read honest

Stratify on a property of the SHUFFLE -- fixed before either arm plays, and therefore not a function
of either arm's result. For this deck the natural ones are:

* the turn metalcraft first went live (3+ artifacts), or `never`;
* the turn the first Puresteel Paladin resolved, or `never`;
* the turn the first Equipment attached.

None is currently emitted. The cheapest shape is a per-game tag on the existing `[win]` line behind a
default-off flag, written from the same place the win turn is recorded, e.g.
`[win] job=... gi=... wt=... tags=metalcraft@3,puresteel@2`. Then the strata are the same games in
every arm by construction, exactly as the paired delta already requires, and the arms' own play
cannot move a game between strata.

Two cautions for whoever builds it.

1. **A tag must not be a DISPLAY string parsed back.** `recorded-events-are-not-a-control-signal`:
   a pin parsed out of a display string inherits that string's filters. Emit the tag from the state,
   next to the win turn, not from a log line.
2. **A tag is still only approximately arm-independent.** The arms play different 60s, so a swapped
   card changes one library slot even under `inherit_numbering`; "the turn Puresteel resolved" can
   differ between arms on the same seed. That is a second-order effect next to the artifact above,
   but the honest form is to stratify on the tag's value IN THE BASE ARM (one fixed partition of the
   game indices, applied to every arm), and to report how often the arms disagree about it.

## Scope

This is not specific to KittyEquipment. Every `deck_compare.py` screen reports means over games, and
every deck has cards whose value is concentrated in a subset of games (sweepers, insurance,
late-game mana). Until a shuffle-property tag exists, the correct answer to *"but how does it do in
the games where X?"* is that this apparatus cannot say, rather than a stratified table.
