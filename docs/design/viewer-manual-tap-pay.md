# Manual tap/pay — the play viewer's mana-allocation fallback

> "I think we probably need an alternative fallback for the user, so they can tap and pay mana as
> they desire. It's a bit tricky perhaps, but we could have it available just for fixing poorly
> allocated taps or mana usage while still being able to give good feedback on the mana usage by
> the engine."
> — USER, EldraziDisplacerFlicker sessions, 2026-09

Status: **shipped**, default ON (`MTG_HUMAN_PRE_TAP=0` disables), human play only.

## What it is

A human playing through `tools/play` can **pre-tap** specific mana sources for specific faces
before a committed line's payments run. Every payment path in the engine spends
`state.floating_mana` **before** it taps anything (`TapForCostSharedOnce` calls
`SpendFloatingTowardCost` on entry), so a source the human taps by hand is the source the next
payment spends. That is the whole mechanism: no new payment mode, no override hook inside the
allocator — just mana that is already in the pool when the allocator arrives.

It is a **fallback, not a new default**. A line that declares no taps is byte-identical to before:
the allocator sees an empty float and allocates exactly as it always did. Sources the human does
*not* pre-tap stay the allocator's to choose, which is what keeps the engine's own mana usage
observable and correctable rather than replaced.

## The token

```
tap=<card name>#<m_number>:<W|U|B|R|G|C>
```

* `<card name>` — the permanent's name. Also the display string in every rejection message.
* `#<m_number>` — **which copy**. `#0` is the wildcard "any untapped copy of that name", which is
  what a hand-written line or a `--scenario` fixture uses; the viewer always stamps the real id,
  because two copies of one land are not interchangeable once one of them is tapped.
* `:<COLOUR>` — the **face** to tap for. There is no default. An unstated or unrecognised colour
  parses to `-1` and is **rejected with a message**, never guessed — guessing is the failure mode
  this feature exists to fix.

`#`, `:` and `=` cannot occur in an MTG card name, which is the same argument `equip=`'s `#`/`@`
and `blink=`'s `@`/`*` suffixes already rely on.

One string, **one parser** (`ParseHumanPreTapToken`, `src/ai/ManaPayment.cpp`), because the token
appears in two different arguments:

| where | why | who reads it |
|---|---|---|
| a `--validate-line` spec token | so the human is told *before* they commit | `ParseLineSpec` → `LineSpec::pre_taps` → `TurnSolver::CheckLine` |
| an entry in the `--cast-order` full-order list | so it reaches the **commit** path, carries a POSITION, and is saved/replayed for free | `AIEngine::ReorderPlanCasts` → `Plan::human_pre_taps` → `ApplyPlanDirect` |

Riding the cast-order list rather than a side channel of its own is the load-bearing choice.
That list is already (a) the human's declared *sequence*, which is what a positioned tap needs,
(b) passed on the commit path (`--validate-line` is not), and (c) recorded verbatim per
main-phase decision in the reference trace and reconstructed verbatim by
`test/viewer_protocol_check.py`. So a pre-tapped line is reproducible from a saved reference with
no new artifact field to keep in step.

## Position

`Plan::PreTap::position` is the number of the human's declared ordered entries that run **before**
the tap, so a tap can sit mid-line:

```
--cast-order "10:*|Clue Token|tap=Azorius Chancery#200000:U|Emiel the Blessed"
```

crack the Clue, *then* tap the Chancery, *then* blink. `ApplyPlanDirect`'s ordered walk (the same
one `Plan::human_action_order` added for interleaved activations, commit `f05d2400`) counts those
entries and flushes each tap the moment its position is reached. Taps declared past the last entry
go in at the end, where the float survives to the next committed line (CR 500.4).

A tap the human puts somewhere it cannot happen — after the action that spends its land — is
**dropped where it stands and reported**, never rescued by moving it. That is the same deal the
human line order already makes: *"It's up to me to make sure the order is correct."*

## What stays engine-owned

`HumanPreTapFaces` is the single legality test, and the decision JSON publishes its answer per
permanent as `taps` (e.g. `"GW"`), so the GUI offers exactly what the engine will accept — there is
no mirrored rule in the viewer to drift. It returns `""` (not hand-tappable at all) for:

| class | why the engine keeps it |
|---|---|
| a tapped permanent, or an opponent's | not a legal tap |
| a non-source | nothing to tap for |
| one-shot sacrifice sources (a Treasure, Lotus Bloom) | the "tap" destroys the permanent; which one-shot a line spends is a payment decision with its own accounting |
| mana-CONVERSION sources (Cascade Bluffs, Arcum's Astrolabe, Ferrous Lake) | each activation consumes a feeder unit — a two-source transaction the token cannot express |
| scaled mana lands (Three Tree City's `{2},{T}`) | the activation has a generic feed cost a pre-tap does not pay, so tapping it here would mint mana the card cannot make |
| `creature_mana_only` sources | that mana can be spent only on a creature spell, and the float carries no such restriction |
| the **coloured** faces of a `colored_creature_only` land (Cavern of Souls, Unclaimed Territory) | same restriction; its painless `{T}: Add {C}` mode **is** offered |
| a source whose gate is dead (an uncharged storage land, a Deathrite with no graveyard land, an Arbor Elf with no Forest) | it makes no mana at all right now |

Everything else a payment can tap, a human can tap — and it goes through **the same mechanic**:
`TapSourceIntoFloat` was extracted verbatim from `TapForCostSharedOnce`'s `tap_source` lambda, so a
hand tap applies painland damage, Grove's opponent lifegain, Aether Hub's `{E}` spend, depletion
decrement, a storage land's full counter burst, a Karoo's two colours, a domain source's
one-of-each, a scaled dork's live count, and the attached land Aura's bonus. A second
implementation of "tap this land" would be four rules bugs for the price of one copy-paste.

## Validation

`CheckLine` **performs the taps for real** on its own state copy (through the identical
`ApplyHumanPreTap` the executor calls) and then grades the whole line on the board those taps
produce. This is the monotone-accepting doctrine applied to payment: a verdict has to be reached on
the board the line will really be paid from. A `tap=` token takes part in no plan-matching test —
a pre-tap changes how a line is *paid*, never *which line it is* — so a pre-tapped line matches the
plan it would have matched anyway.

An impossible tap is `Illegal` with the reason verbatim (`"can't tap as asked: 'Kitchen' cannot
produce {R} (it makes {GU})"`). Dropping it and grading the line as if the human had not asked
would silently hand the allocation back to the engine — precisely the behaviour being overridden.

## The UI

Minimal, and built out of existing viewer idioms only (board clicks + a central dialog; never the
history panel — see the play-viewer decision principle).

* **`⛏ Tap mana`** toggles the mode, in the Lands zone header beside the floating-mana pips —
  which is exactly what it edits. Offered only on a main-phase frame that actually has a
  hand-tappable source, so it is never a dead control.
* While armed, every source carrying `taps` gets a `⛏` badge and a click handler, the same
  arm-then-click shape as Land's Edge, the Vial deploy mode and the armed blink. **Nothing about a
  plain, unarmed board click changes** — quietly turning a land click into a mana commitment would
  alter a gesture nobody asked to change.
* One face → queued straight away. Several → a small central dialog (`{G} Green` / `{W} White` /
  Cancel), the `.pbtns` button-row idiom.
* The tap becomes a plan chip — `⛏ tap Conservatory → {W}` — at its queued position, removable with
  the chip's `✕` like anything else. Its position among the chips is meaningful.

## Feedback: telling the human's taps from the engine's

The other half of the request. Two surfaces:

* **Before commit** — the plan chips name every forced tap and its face.
* **After commit** — each performed tap emits a `mana` play event (`manual tap: Conservatory for
  {W}`), so the turn history names the human's taps. Every *other* tapped permanent on that board
  was the allocator's own choice. Without this the two are indistinguishable after the fact: the
  float a forced tap makes is spent by the very next payment, so the end-of-turn board looks the
  same either way.
* **For debugging** — `MTG_PRE_TAP_TRACE=1` prints one stderr line per tap and per rejection.

## Guards

| layer | what it pins |
|---|---|
| `test/manual_tap_check.py` (in `test/viewer_checks.sh`) | a live commit with pre-taps taps exactly those sources for exactly those faces; the same line without them takes the engine's allocation; `MTG_HUMAN_PRE_TAP=0` is a real off switch; six rejection classes each report their reason; the same token declared in two positions runs in two places |
| `test/scenarios/edf_manual_tap_*.json` (in `test/scenarios.sh`, gated by `regression.sh`) | the accepting case and the two rejecting ones, on a synthetic board, deterministically |
| `test/viewer_linebuild_check.js` | `encodeLine` still rebuilds every recorded line (pre-taps are additive; no reference has one) |

## Byte-identity

Every site is gated on `HumanPreTapEnabled() && HumanPlayActive()`, and the search never writes
`Plan::human_pre_taps`, so rollouts, autonomous play, the regression digests and every saved
reference are byte-identical by construction. `PreTap` is deliberately absent from `PlanSignature`
and `PlansEqual` for the same reason `human_action_order` is: it is not part of the search's plan
space, it is a payment instruction riding on top of one.

## Deliberately not done

* **Untapping / undoing a tap mid-line.** A committed tap is committed; the chip's `✕` removes it
  before commit, which is the only point at which it is a choice.
* **Directing *which* pip a floated unit pays.** The float is a pool; `SpendFloatingTowardCost`
  decides the pip order (and its human-play holds — `MTG_HOLD_C_FOR_SINK`, `MTG_HOLD_C_FOR_PIPS` —
  already encode the user's colourless-retention doctrine). Pre-tapping controls *supply*, which is
  where the reported mis-allocations actually were.
* **A pre-tap of a conversion / feed-cost / one-shot source.** See the table above. Each needs a
  second permanent or destroys itself, so the token cannot honestly express it; they stay with the
  allocator and the viewer does not offer them.
