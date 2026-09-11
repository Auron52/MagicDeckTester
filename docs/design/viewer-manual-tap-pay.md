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
tap=<card name>#<m_number>:<FACE>[+<AURA>[+<AURA>...]]
```

* `<card name>` — the permanent's name. Also the display string in every rejection message.
* `#<m_number>` — **which copy**. `#0` is the wildcard "any untapped copy of that name", which is
  what a hand-written line or a `--scenario` fixture uses; the viewer always stamps the real id,
  because two copies of one land are not interchangeable once one of them is tapped.
* `<FACE>` — the **face** to tap for, one of `W U B R G C`. There is no default. An unstated or
  unrecognised colour parses to `-1` and is **rejected with a message**, never guessed — guessing is
  the failure mode this feature exists to fix.
* `+<AURA>` — zero or more, one per **any-colour land Aura** on the host, in `AnyColorLandAuras`
  (battlefield) order. `W U B R G` only. See below.

`#`, `:` and `=` cannot occur in an MTG card name, which is the same argument `equip=`'s `#`/`@`
and `blink=`'s `@`/`*` suffixes already rely on. `+` **can** occur in one (`+2 Mace`), and that is
fine because it is never parsed in the name: the name is everything left of the *final* `:`, and the
`+` split happens strictly inside the colour field to the right of it.

### The Aura's colour is a second choice on the same tap

Fertile Ground and Trace of Abundance read *"whenever enchanted land is tapped for mana, its
controller adds an additional **one mana of any color**"* (`land_aura_produces: []`). CR 106.1b makes
that colour a choice as the ability resolves — and before 2026-09-11 nobody made it: `LandAuraAddToPool`
credited the unit as `wild`, straight into `state.floating_mana`. That is a **doctrine violation on
top of a missing decision**: pools hold typed mana, `wild` is the search's optimism and never a real
pool's content ("Generic mana exists only in COSTS"). USER: *"Tap mana doesn't work for Fertile
Ground. There should be a choice there."*

```
tap=Brushland#7:W          -> float{w1 *1}      the Aura's mana is WILD, nobody chose  (legacy)
tap=Brushland#7:W+U        -> float{w1 u1}      the human chose {U} for the Fertile Ground
tap=Aether Hub#3:C+R       -> float{r1 g1 c1}   Hub {C}, Trace {R} chosen, Wild Growth {G} fixed
```

Three properties make this safe to add to a shipped token:

* **Positional, over the ANY-COLOUR auras only.** A fixed-colour Aura (Wild Growth `{G}`, Overgrowth
  `{G}{G}`) is not a choice, so it consumes no `+` slot — the third example above has two Auras on
  one land and exactly one suffix. `AnyColorLandAuras` is the single walk all three readers use (the
  decision JSON, the legality test, the credit), so "the second `+X` is the second Aura" means one
  thing everywhere.
* **Omitting them keeps the old behaviour exactly.** No `+` → `chosen == nullptr` → `wild`, as
  before. Every reference saved before this replays byte-for-byte, and no reference has one (the
  feature post-dates them all).
* **An impossible choice is `Illegal` with the reason verbatim**, like every other arm: `'Fertile
  Ground' on 'Brushland' adds one mana of any COLOR, so it cannot add {C} (it makes {WUBRG})`;
  `'Kitchen' carries no land Aura that adds one mana of any colour, so there is no Aura colour to
  choose`; `'Aether Hub' carries 1 any-colour land Aura(s) but the tap names 2 Aura colour(s)`.
  `{C}` is refused on purpose: *any colour* is WUBRG and colourless is not a colour (CR 105.1) —
  the same reason `LandAuraColorMask` sets five bits and the credit never touches `wild_c`.

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
no mirrored rule in the viewer to drift. Two companion keys ride beside it, both additive and both
emitted only when they say something (so no other deck's frame moves):

| key | from | what the dialog does with it |
|---|---|---|
| `tap_auras: [{name, faces}]` | `HumanPreTapAuraFaces` | asks one more question per any-colour Aura |
| `tap_energy: <n>` | `energy_per_colored_tap` | says a COLOURED tap costs `{E}` and the `{C}` mode does not |

`taps` already answers the energy question by itself and always did: `EffectiveProduces` strips the
coloured modes while the controller cannot pay, so a spent-out Aether Hub publishes `"C"` and a live
one publishes `"CWUBRG"`. `tap_energy` exists to *explain* that, not to gate it.

It returns `""` (not hand-tappable at all) for:

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
* Nothing to ask (one face, no any-colour Aura) → queued straight away. Otherwise a small central
  dialog (`{G} Green` / `{W} White` / Cancel), the `.pbtns` button-row idiom.
* **One tap can need several picks, and they are STEPS OF ONE DIALOG** — the land's face, then one
  per any-colour Aura on it (`S.tapPick.step` / `.picks`). They are one gesture: a half-answered tap
  is not a weaker tap, it is the *old* behaviour (`wild`), so Cancel abandons the whole thing rather
  than queueing what was answered so far.
* **The colours the queued line still needs sort first and are labelled "— needed".** That demand is
  `LB.untapNeed` over the current queue — the very function the `need=` continuation token is built
  from, so "what this line still wants" means one thing in the viewer. It is an *ordering and a
  label*, never an auto-pick: the engine silently choosing is the bug being fixed.
* The tap becomes a plan chip — `⛏ tap Conservatory → {W}`, or `⛏ tap Brushland → {W} +{U}` when an
  Aura colour rides along — at its queued position, removable with the chip's `✕` like anything
  else. Its position among the chips is meaningful.
* The Lands header's `⚡` energy pip renders **whenever the key is present, including at zero**
  (`energy: 0` is grey `⚡0`). See "Energy" below.

## Feedback: telling the human's taps from the engine's

The other half of the request. Two surfaces:

* **Before commit** — the plan chips name every forced tap and its face.
* **After commit** — each performed tap emits a `mana` play event (`manual tap: Conservatory for
  {W}`), so the turn history names the human's taps. Every *other* tapped permanent on that board
  was the allocator's own choice. Without this the two are indistinguishable after the fact: the
  float a forced tap makes is spent by the very next payment, so the end-of-turn board looks the
  same either way.
* **For debugging** — `MTG_PRE_TAP_TRACE=1` prints one stderr line per tap and per rejection.

## Energy — and the payment bug the invisible display was hiding

Two separate defects, reported together on 2026-09-11 and fixed together because the first is what
made the second unreadable.

**1. `energy` was emitted only when nonzero.** USER: *"I cannot see my energy for Aether Hub."* The
`me` frame's emit was `if (me.energy_counters > 0)`, and `energyHtml` matched it with `e <= 0 ->
render nothing` — so a board with a spent-out Hub showed *nothing at all*, which is exactly what a
deck with no energy in it shows. Zero is the number worth seeing: it is the difference between a
rainbow land and a Wastes. The gate is now the **board** (`energy_counters > 0` *or* a controlled
permanent with `etb_energy` / `energy_per_colored_tap`), so the key is present for the whole game on
a Hub board and absent for every deck that has no energy card — additive, nothing else moves.

**2. A GENERIC pip was spending `{E}`.** `logs/play/rejections/EldraziDisplacerFlicker_cod_s13_gi12_t4.json`
— USER: *"I should have the energy to drake, but I cannot."* Seed 13 gi 12, `--choices
0,1,2,0,-1,-1,26,3,-1,-1,13`. The turn-3 Overgrowth (`{2}{G}`) payment, under `MTG_TAPDBG=1`:

```
[tapdbg] tap Brushland col=4 energy=1              {G} -- pays the {G} pip (and 1 painland damage)
[tapdbg] tap Mariposa Military Base col=5 energy=1 {C} -- pays half the {2}
[tapdbg] tap Aether Hub col=4 energy=1             {G} -- pays the OTHER half of the {2}, for {E}
```

Two green taps for a one-green cost: the second went to a generic pip, and it burned the board's
only energy counter. Next turn the Hub read as a plain `{C}` land, `taps: "C"`, no `energy` key at
all, and `--validate-line "cast=Peregrine Drake"` came back *"no untapped source produces blue
mana"* — the Hub's energy mode **was** the deck's blue.

The fix is one branch in `DripLandAnyPipColor`, beside the two that were already there. That
function is the generic pip's colour chooser, and it already encodes exactly this shape twice: a
Grove of the Burnwillows and a painland each have a **separate, free `{T}: Add {C}` ability** next to
a coloured one that costs something, and a generic pip does not need a colour, so it takes the free
mode. Aether Hub is the third instance and was simply never added. (The backtracker's twin hoist,
beside the painland's, went in with it.)

Why it fired only for a human: the coloured pick handed to `DripLandAnyPipColor` is
`LineDemandAnyPipColor`, which is `HumanPlayActive`-gated and replaces the produces-order default
with *a colour this line still owes* — `{G}`. Autonomous play keeps `prod[0]`, and Aether Hub's
`produces` leads with `{C}`, so the autonomous payment was already taking the free mode and GT is
byte-identical. It is fixed in `DripLandAnyPipColor` anyway, with the others, because the free
ability existing is a **modelling** fact and not a play preference — it has to hold on whichever
path reaches it. `MTG_ENERGY_C_MODE=0` restores the coloured tap for a one-binary A/B.

The *coloured*-pip half of the doctrine needed no new code: the Hub is a six-face rainbow, so
`ManaSourceRank`'s flexibility ladder already taps every less-flexible source ahead of it, and a
`{G}` pip on a Brushland board takes the Brushland. Pinned as a check arm rather than assumed.

## Guards

| layer | what it pins |
|---|---|
| `test/manual_tap_check.py` (in `test/viewer_checks.sh`) | a live commit with pre-taps taps exactly those sources for exactly those faces; the same line without them takes the engine's allocation; `MTG_HUMAN_PRE_TAP=0` is a real off switch; six rejection classes each report their reason; the same token declared in two positions runs in two places |
| …its **land-aura** arms (`claude_s6_gi5` T4: Brushland+Fertile Ground beside Kitchen+Overgrowth) | `tap_auras` lists the any-colour Aura and **not** the fixed one; a chosen colour floats `{W:1,U:1}` with **`wild` zero**; the same token without a `+` still floats `{W:1, wild:1}`; Overgrowth still credits 3 green; three impossible choices each rejected with their reason |
| …its **Trace of Abundance** arm (`claude_s1_gi0` T3: a Hub carrying a Trace *and* a Wild Growth) | the `+` suffix is matched against the ANY-COLOUR aura only, so `:C+R` floats `{r1 g1 c1}`; the Hub publishes `tap_energy` and `energy` even at **zero**; at zero energy only the free `{C}` face is offered and a coloured hand-tap is refused with its reason |
| …its **energy-vs-generic-pip** arm (seed 13 gi 12, the user's rejection log) | the Overgrowth payment taps the Hub `col=5` (free `{C}`), the counter survives, the Hub still reads `CWUBRG` next turn, **Peregrine Drake validates**, and `MTG_ENERGY_C_MODE=0` reproduces the reported failure |
| `test/scenarios/edf_manual_tap_*.json` (in `test/scenarios.sh`, gated by `regression.sh`) | the accepting case and the two rejecting ones, on a synthetic board, deterministically |
| `test/viewer_linebuild_check.js` | `encodeLine` still rebuilds every recorded line (pre-taps are additive; no reference has one), **and** `checkPreTapToken` pins the `+<AURA>` suffix — including that it is absent when there is none, which is the back-compat guarantee no reference can test |

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
