# Mana-payment rollback: a rare, needs-based repair of past generic-pip payments

**Status: DEFERRED — designed, not built.** Origin: USER, 2026-10-05, prompted by the Snow
reference `references/Snow/claude_s4_gi3.json` (see "Worked example"). Self-contained.

## The idea, in the user's words

> *"It should be needs based, so I can't get behind a general rule like that. However, on most decks
> colourless should indeed be spent before a colour and similarly common colours should be spent
> before uncommon when they may be needed. ... I think we need a way to look back 1-2 breakpoints to
> make mana sufficiently accurate. My idea is to have a reasonably accurate set of heuristics and
> then record which mana we threw away on generic costs and potential colours it could have
> generated instead. Then on a future line we can consider rolling back our mana spend only when it
> is required."*

> *"The tricky part with the backtrack idea is we need to be super careful to avoid it becoming
> degenerate. Hence the idea that it is an uncommonly required fix. We want our rules to be good
> enough to only occasionally really need it. Ideally there would also be a way in the viewer to
> backtrack payments like this so the user can see what the engine does."*

So there are two layers, and the order of priority matters:

1. **Heuristics first, and they carry the load.** Generic pips are paid by a needs-based rule: on
   most decks colourless before a colour, and a common colour before an uncommon one *when the
   uncommon one may be needed*. NOT a blanket rule — see "Why not a blanket rule" below.
2. **Rollback second, and only rarely.** Every payment records which sources it spent on GENERIC
   pips and what each of those sources could have produced instead (plus which untapped
   alternatives existed). When a later line in the same turn is short of a colour, and a recorded
   generic payment could have been made from a different source that is still untouched, the
   engine may retroactively swap the two — but only when that line actually needs it.

## How this differs from what already ships

Every payment mechanism shipped so far is **forward-looking reservation**: it must know the demand
at payment time.

| mechanism | what it does | doc |
|---|---|---|
| whole-turn batch payment (`BatchPrepayMainCasts`) | one joint tap solve per main phase | `mana-source-reservation.md` |
| scarcity-first ordering + the 15-lever overhaul | rank sources, hold classes | `mana-order-and-reserve-overhaul.md` |
| `MTG_M2_PAYLOAD_RESERVE` | reserve for the best post-combat payload | `main2-aware-mana-choice.md` |
| `MTG_LINE_SURPLUS_GENERIC` | generic pips spend (have − demanded) surplus first | USER doctrine 2026-09-10 |

Rollback is the complement: **backward repair, on demand.** It covers exactly the cases a forward
reservation cannot, the canonical one being demand that did not exist at payment time because the
payment itself funded the reveal that created it.

## Worked example (why it exists)

`Snow/claude_s4_gi3`, turn 5. Board: two Scrying Sheets (#38 just played, #39), Snow-Covered
Forest #45, Snow-Covered Islands #50 and #56. Scrying Sheets: `{1}{S}, {T}: look at the top card; if
it is snow, put it into your hand`, and `{T}: Add {C}`.

* **Human:** activated #38; paid `{1}{S}` with #39's `{C}` + Island #50; Forest #45 paid Boreal Druid.
  Island #56 stayed up. The dig found **Frost Augur ({U})**, cast off #56.
* **Engine:** activates #39 and pays `{1}{S}` with **both Islands**, leaving #38 (colourless only)
  up. Frost Augur is uncastable; the recorded plan disappears (`nplans 2->1`). The reference has
  never replayed on any commit (checked at 09-06, 09-08 and HEAD).

Note what the surplus rule does here: two U against one C, so it spends U — it is reasoning
correctly about the *known* demand, which is zero. The demand appears only after the dig.

## Why not a blanket "colourless first" rule

The same game shows it. Had the plan activated *both* Sheets, #38's `{C}` is precisely the source
you must not spend, because tapping it for mana consumes the `{T}` its own ability needs. A source's
colourless output is cheap only if nothing else wants that source. That is a needs question, which
is why the heuristic layer must be needs-based, not ordinal.

## The hard constraints

### 1. It must stay RARE — degeneracy is the main risk

A rollback that fires often means the heuristic layer is wrong and the rollback is doing its job
for it: a hidden second payment solver, unbounded in cost, and invisible in the census. So:

* **The firing rate is the success metric.** Count rollbacks per turn and per game (a census
  counter alongside the existing tap-backtrack counters), by deck. A high or rising rate is a defect
  report against the heuristics, not a sign the rollback is working.
* **Set a tripwire.** Decide a per-deck ceiling (e.g. rollbacks on ≤ a few % of turns) and fail the
  measurement loudly above it, the way the batch heartbeat surfaces starvation. Tune the
  heuristics until every deck is under it *before* adoption.
* **Bound the work.** At most one swap set per short line, a hard cap on ledger length, and no
  nested rollback (a rolled-back payment cannot itself trigger another rollback). The tap
  backtracker's history (`tap-backtrack-blowup.md`: one payment solve ran for 14 hours) is the
  warning: no unbudgeted search inside payment.

### 2. Clairvoyance across a reveal

The breakpoints you would look back across are usually reveals (draws, digs). Rolling back across one
lets a payment act on information it did not have. In the worked example the payment funded the
very dig that created the demand. Two classes:

* **Dominant swaps — not clairvoyant.** Keeping a source that produces `{U}` open instead of one that
  produces only `{C}`, when the `{C}` source had no other planned use, is weakly better *whatever the
  reveal shows*. The heuristic layer should already make these up front; a rollback that does one
  is a heuristic miss to log, not a peek.
* **Non-dominated swaps — clairvoyant.** U vs G when either might have been needed. Across a reveal,
  either forbid the swap or charge it to the clairvoyance account the repo already keeps. Never
  silently allow it.

### 3. The ledger must prove the alternative is still valid

A swap is legal only if, for the alternative source, all of these hold:

* at payment time it was untapped and able to produce that mana (summoning sickness, restrictions
  such as Unclaimed Territory);
* now it is still untapped and in the same state: not tapped, sacrificed, bounced, attacked with,
  or otherwise used since;
* and the freed source is fine to untap (nothing since depended on it being tapped).

Tap side effects must replay: pain lands cost life, and some cards trigger on "whenever you tap a
land for mana". A swap that changes those changes the game.

### 4. Scope and placement

* **Window: one turn.** Everything untaps at the untap step, so the ledger never needs to outlive
  the turn. "1–2 breakpoints back" is in practice "since the turn's first payment".
* **Lockstep.** Executor and rollout must both do it, identically, like batch payment.
* **No-greedy rule.** This lives inside mana payment (a standing exemption) and repairs a payment;
  it must never choose a line. Get the user's confirmation that this placement fits the rule
  before building.

## Viewer: let the user backtrack payments too

The user wants to see what the engine does and do the same by hand:

* When the engine applies a rollback, the event log shows it explicitly, e.g. *"re-tapped: Scrying
  Sheets #38 for {C} instead of Snow-Covered Island #56 (needed {U} for Frost Augur)"*. Never a
  silent change of tap state.
* A human can trigger the same repair: on a main-phase frame where a plan is short of a colour that
  an earlier same-turn generic payment could have supplied, offer it as an option (for example
  "re-pay earlier: …"), subject to the same validity checks. It is recorded in the reference like
  any other decision, so references replay through the same mechanism.
* This also gives references a principled answer for the class Snow `s4_gi3` belongs to: a human
  line that kept a flexible source up, which the engine's payment then failed to reproduce.

## Measurement / adoption

The standard bar applies (CLAUDE.md, "no greedy" section): every deck's aggregate net ≤ 0 vs the
committed tree on a large held-out sample. In addition, report the **per-deck firing rate** and
show it under the tripwire. A quality win that comes with a high firing rate is not adoptable as is:
it means the heuristics are owed work first.

Order of work: (1) build the needs-based generic heuristic and measure it alone; (2) add the ledger
and counters with rollback OFF, to measure how often it *would* fire; (3) only then enable rollback.
