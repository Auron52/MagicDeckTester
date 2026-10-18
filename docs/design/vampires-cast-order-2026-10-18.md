# Vampires: the AI-drafted cast order and discard buckets (2026-10-18) -- AWAITING THE USER

**Status: DRAFT, implemented DEFAULT OFF, not A/B-measured** (section 4 has the byte-identity check and a pre-fix liveness probe; section 5 has the A/B, which waits for the convoke / payer fixes). Both are USER-owned decisions (CLAUDE.md, "A
USER-OWNED DECISION IS NEVER SETTLED BY AN AGENT"). The USER's standing ask (2026-10-14): *"The generic
order is never a good option in my opinion. It should be edited by you and then presented to me for
additional feedback ... Major bonus points if that can largely be constructed by AI as well."*

| lever | heurarm slot | default | what it turns on |
|---|---|---|---|
| `MTG_VAMPIRES_ORDER=1` | `VAMPIRES_ORDER` | OFF | the total cast order (section 2) + the main-phase view in `--cast-order-report` (section 2.3; display only) |
| `MTG_VAMPIRES_BUCKET_DISCARD=1` | `VAMPIRES_BUCKET_DISCARD` | OFF | the cleanup-discard buckets (section 3) + the order of the searched outlet discard pins (section 3.4) |

Deck: `decks/Vampires/Vampire.cod` (ledger `docs/design/analysis-Vampire.md`). Provider `VampiresProvider :
VialProvider` (`src/ai/DecisionProviders.{h,cpp}`). Every card below was read from
`src/cards/data/cards.json` (mana cost, P/T, oracle text, params), not from memory.

## 1. What plays today

- **Cast order: the generic tiers.** Every creature ranks 10 and ties, so the casts of one main go in
  plan (= hand) order. Aether Vial ranks 20 (after the creatures). The order is not total. (The Knights
  clauses VampiresProvider inherits from VialProvider are param-gated and inert here.)
- **Main phase: both mains searched for every card.** The deck plays a second main
  (`uses_second_main=yes`), and VampiresProvider opts into no main-phase filter.
- **Cleanup discard: slivers_vial's buckets** (`MTG_SLIVERS_BUCKET_DISCARD`, inherited from VialProvider).
  That is a slivers rule: a 3-turn curve fill with lords first, land targets derived from the dominant
  creature mana value. It knows nothing of madness or Bloodghast. (Ledger question 12.)
- **Outlet discard pins** (Stromkirk Condemned, Falkenrath Pit Fighter, the Blood token): searched, one
  variant per distinct hand name, ordered by the A2 draft (`MTG_VAMP_DRAFT_ORDERS`, default ON): a
  Bloodghast a held land will return, then lands past four, then cards the current mana cannot cast,
  then the inherited slivers ranking.
- **Trailing activations** (also `MTG_VAMP_DRAFT_ORDERS` + the B count bands): Keeper token taps, then
  Blood creation, then Condemned's pump, then Blood cracks, then the steal, then the transform, then Pit
  Fighter. Not changed here. They are listed in section 2.1 so the whole turn reads as one order.

## 2. The drafted cast order (`MTG_VAMPIRES_ORDER`)

### 2.1 The order

`>` = cast before. Ranks are read off params (house style); names break ties only.

| # | card | rank key | range (where it could go with no effect) | why here |
|---|---|---|---|---|
| 0 | the land drop | - | first | No CAST in the list draws (Pit Fighter's draw and the Blood crack are trailing activations; Asylum Visitor's is an upkeep trigger), so nothing wants to precede it. NOT pinned (`LandDropCastOrderRank` stays -1). Bloodghast's graveyard landfall is a real reason to play the land AFTER the activations, and the engine keeps that line (`PutsGyLandfallFeeder`'s "land after the activations" clone). |
| 1 | Vampire Socialite | 1 (an `opp_lost_life_*` counter param) | 1-13 | Order-free for hand casts. Its ETB puts a counter on every Vampire already there, and its as-enters clause on every later one, so each cast Vampire gets one counter either way. It goes first because "watcher before what it watches" is the robust reading: the as-enters half also reaches entries outside the cast list (madness casts in the trailing pass, a Bloodghast returned by a later land, Keeper / Mavren tokens). |
| 2 | Falkenrath Gorger | 2 (`grants_madness_creature_subtype`) | 1-13 | Order-free. Every discard (Condemned / Pit Fighter / Blood in the trailing pass, the cleanup) happens after every cast. It goes ahead of the outlet bodies, the reading the order would need if an outlet ever fired mid-sequence. |
| 3 | Stromkirk Captain | 3 (lord + `grants_first_strike`) | 1-13 | Static lord (CR 611.3): the pump and the first strike apply to every attacker whatever the entry order. It leads the lords. |
| 4 | Legion Lieutenant | 4 (lord) | 1-13 | Static lord. |
| 5 | Captivating Vampire | 5 (lord + `steal_tap_count`) | 1-13 | Static lord. The steal is a trailing activation, after every cast. |
| 6 | Falkenrath Pit Fighter | 20 + mv 1 | 1-13 | Order-free body. Its activation trails (and needs combat damage first). |
| 7 | Vampire of the Dire Moon | 20 + mv 1 | 1-13 | Order-free body. |
| 8 | Asylum Visitor | 20 + mv 2 | 1-13 | Order-free body. Its draw is an upkeep trigger. |
| 9 | Bloodghast | 20 + mv 2 | 1-13 | Order-free body. |
| 10 | Stromkirk Condemned | 20 + mv 2 | 1-13 | Order-free body. Its pump trails the casts, so it reaches every creature this main put down. |
| 11 | Drana, Liberator of Malakir | 20 + mv 3 | 1-13 | Order-free body. Her trigger is a combat-damage trigger. |
| 12 | Mavren Fein, Dusk Apostle | 20 + mv 3 | 1-13 | Order-free body. Her trigger fires at declare-attackers. |
| 13 | Bloodline Keeper | 20 + mv 4 | 1-13 | Order-free body. Token and transform are trailing activations. A sick Keeper's {T} is not live, so it is a FREE convoke body for a Baron cast after it. |
| 14 | Markov Baron | 40 (`convoke`) | 14 (fixed) | **The one load-bearing slot.** Convoke: "each creature you tap while casting this spell pays for {1} or one mana of that creature's color." A body cast earlier in the same main is untapped and summoning sick, so tapping it costs nothing. Baron after every other creature lets the line pay with every such body. Ahead of a body, the cast pays that much more from lands or is unpayable. |
| 15 | Aether Vial | 50 (`upkeep_adds_charge`) | 15 (14-15 under the batch prepay) | Every land in the 20 pays its {1} (each makes {C} or {B}). The creatures' pips draw on subsets: {W} only from the 12 creature-only rainbow lands, {R} from those plus Blackcleave Cliffs, {B}{B}. So the Vial pays last, from whatever the creatures left. The whole-turn batch prepay makes this inert. It matters on the per-cast fallback and in the enumeration's sequential payability walk. A Vial cast this turn has 0 counters and puts nothing, so no same-turn use is lost. |
| 16+ | trailing activations | (A2 / B, not this lever) | - | Keeper token taps > Blood creation (its cost falls per Vampire) > Condemned's pump > Blood cracks > the steal > the transform > Pit Fighter (a sacrifice lowers the count). |

- **Vial puts** are sequenced at their creature's rank (USER ruling 2026-10-06), so a Vial-put Baron
  goes last too.
- **A card the list does not hold** keeps a deterministic place. A creature goes at its mana value
  among the bodies, so before Baron. A noncreature goes after the Vial. The NAME breaks every tie
  (`CastOrderTotalByName`), never the hand order.
- **The tie-break inside 6-13 is cheapest first, then by name.** It is a choice, not a finding: no rule
  in the list orders these cards. Say if you want a different fixed sequence.

### 2.2 What changes and why it matters

1. **Markov Baron after the bodies.** This is the substantive change. Today Baron ties with every
   creature at 10, so "Baron, then the 1-drops" is the realised order whenever Baron sits earlier in the
   hand. The convoke arms then cannot use those bodies, so the main pays more from lands or drops a cast.
   **This only becomes observable once sweep defect B is fixed** ("convoke ignores creatures cast /
   Vial-put earlier in the same plan", `docs/design/vampires/claude_sweep_1.md`). On the current binary
   the convoke count ignores same-plan bodies in every order, so the slot measures as nothing. That is
   one reason the A/B waits for the fix track.
2. **The Vial pays last.** A small robustness gain on the per-cast payment fallback (see defects F and
   L on the payer). Today the Vial already ranks after the creatures (generic 20), so this keeps today's
   behaviour and states why.
3. **Total.** No main's order depends on the hand order any more (the cross-deck ruling).
4. **Nothing else in the list is order-sensitive in the cast list.** Lords are static, Socialite's two
   clauses cover both sides, and every outlet, steal, token and transform trails the casts. I looked
   for a turn the generic order loses through a trigger or a missed pump and found none. The one
   inexpressible line I found is outside the cast order: "steal with Captivating Vampire, THEN cast
   Socialite" (to put her ETB counter on the stolen creature) cannot be built in one main, because
   activations always trail the casts. It is rare and minor. I am flagging it, not fixing it.

### 2.3 The main-phase view (yours to decide; display only today)

`MainPhaseOverride` carries this view under `MTG_VAMPIRES_ORDER=1`, so `mtg decks/Vampires/Vampire.cod
--profile decks/Vampires/Vampire.profile.json --cast-order-report` prints it. **It does not change play.**
VampiresProvider opts into neither `ClassifiesMainPhases` nor `CondemnsPassedMainPhase`, so both mains
stay searched for every card. Adopting it in play would be a lossy narrowing that needs its own A/B
(question 2).

| card | view | why |
|---|---|---|
| Vampire Socialite | **m2** | Both clauses need "an opponent lost life this turn", which in this list only combat damage sets (our own Voldaren / Visitor payments do not). Cast in main 1 she is a plain 2/2 menace. |
| Falkenrath Pit Fighter | **m2** | Its draw needs the opponent to have lost life. The body is sick either way. Its only main-1 role is as a convoke body or a Vampire for the count. |
| Stromkirk Captain | **m1** | Pumps THIS combat (+1/+1 and first strike to every other attacking Vampire). On a Drana turn see the note below. |
| Legion Lieutenant | **m1** | Pumps this combat. |
| Mavren Fein | **m1** | "Whenever one or more nontoken Vampires you control attack": she need not attack herself, so cast in main 1 she makes a token THIS combat. |
| Aether Vial | m1 | Nothing to wait for. |
| Markov Baron | both | A lord now, vs every untapped body free for convoke after combat (the attackers are tapped, the rest have no other use). The search decides. |
| Captivating Vampire | both | A lord now, vs the steal after combat (untapped non-attackers pay it). |
| Stromkirk Condemned | both | A pre-combat pump for the attackers, vs a post-combat madness outlet. |
| Bloodghast | both | m1 when the opponent is at 10 or less (haste), else m2 or as discard fodder. |
| Falkenrath Gorger | both | m1 when a main-1 discard (Condemned) wants madness live, else either. |
| Bloodline Keeper, Drana, Asylum Visitor, Vampire of the Dire Moon | both | Summoning sick and no main-1 role. They prefer m2 once Socialite is out and the attack connects: each enters with a +1/+1 counter. |

**Stromkirk Captain vs Drana (ledger question 9).** With Drana attacking:
- Captain cast **in main 1**: every other attacker gets +1 from the lord in the first-strike step, and
  Drana herself gets +1. Drana's counters then land on creatures that have already dealt their damage,
  so they add nothing this combat.
- Captain held to **main 2**: the N other attackers deal +1 each in the regular step from Drana's
  counters, and Drana gets nothing.
- The counters persist in both cases, and the Captain is out for the next turn in both cases.

So main 1 is ahead by exactly **+1 damage this combat** (Drana's own lord bonus). Main 2 can compete
only when Socialite is out and the opponent has lost life, because then the Captain enters with a
permanent +1/+1 counter. **My view: main 1, keep both searched** (the default today). The split is yours.

## 3. The drafted discard buckets (`MTG_VAMPIRES_BUCKET_DISCARD`)

### 3.1 Why this deck needs its own shape

The baseline discard analysis found **0 real cleanup sheds in 400 d3 games, but 64,621 rollout sheds**
(all with fewer than 4 lands out). The rule mostly shapes the SEARCH'S ESTIMATES (every playout's
cleanup takes index 0) rather than the executed sheds.

In this list a discard is often not a loss:
- Falkenrath Gorger gives every Vampire creature card madness, and Markov Baron and Asylum Visitor have
  their own. A creature the untapped mana can pay is CAST when discarded. At cleanup it is then a turn
  early: it can attack next turn.
- Bloodghast returns from the graveyard on the next land.

The inherited slivers rule sees neither.

### 3.2 The buckets

Quota-first, net of the board, distance-to-playable, ONE index per consultation (5i rules 1-5).

| bucket | quota | rule |
|---|---|---|
| **FREE** | none (shed first) | (a) **at cleanup only**, a creature card madness can cast right now: its madness cost (own, or Gorger's) payable from the untapped mana, with convoke counted for Baron (every body is free at cleanup). Best value first: the shed deploys it. (b) a Bloodghast with a land in hand: it returns on that land for nothing. |
| **DEAD** | 0 | an Aether Vial beyond the first (board copy included); a legendary we already control (Drana, Mavren) or a second copy of one. |
| **MANA** | lands to **4 counting the board** | Colour coverage first: a land that makes {W} (only the rainbow lands) while a held card needs {W} and no board land makes it; the same for {R}. Then the most flexible: Secluded Courtyard (also pays Pit Fighter's {R} and the transform's {B}) > Voldaren Estate (the Blood sink, a pay-1-life colour) > Unclaimed Territory > Blackcleave Cliffs (marked down once it would enter tapped) > Swamp. |
| **VIAL** | 1, only EARLY (turn <= 2, none on board) | kept behind a two-threat floor; after turn 2 it sheds before every threat. A Vial cast on turn 3 or later starts putting creatures a turn after they would have been cast anyway. |
| **THREATS** | the catch-all; a floor of 2 | ordered by **EV = castability x value**. Castability is the shared `discard_dist` model: the board's mana, the KEPT lands, the library's land and colour density, discounted per turn of delay. Baron's cost is taken net of the bodies that convoke it next turn. A mana value a Vial on the battlefield puts next turn counts as castable (1.0). A held Vial that is being kept makes a 1-drop castable from the turn after it lands (>= 0.75), whatever its colour: the Vial ignores the {R} a two-Swamp board cannot make. |

**Value** (param-derived; you review it): printed power + toughness; a lord's bonus x3 (+6); first
strike / steal / convoke +1; Keeper's token (+4) and transform (+2); Drana's counters, Mavren's token,
Socialite's counters +3; Condemned's pump and Pit Fighter's draw +2; Visitor's draw +1; Gorger +3 while a
held Vampire would use the grant and no granter is out (+1 otherwise, +0 with one out); Bloodghast -3
(the deck's best fodder). Resulting values: Keeper 12, Captain / Captivating / Baron 11, Lieutenant 10,
Drana 8, Mavren / Socialite 7, Condemned 6, Gorger 3-6, Pit Fighter / Visitor 5, Dire Moon 2, Bloodghast 0.

### 3.3 The shed order

FREE (at cleanup: madness-castable, best first; then a Bloodghast with a land) -> DEAD -> lands past the quota
(worst first) -> a late Vial -> threats past the floor (lowest EV first; then the dearer card, then the
name) -> an early Vial -> the two floor threats -> the kept lands (last picked first).

`CleanupDiscardShedStable()` is **false** while the rule is on. A shed's madness cast spends the
untapped mana the FREE tier reads, so a multi-card cleanup re-asks after every shed. (In the rollout's
cleanup loop the madness queue resolves after the last shed, so a second shed still sees the first
one's mana. That is a known approximation for the rare 2+-card cleanup.)

### 3.4 The outlet discard pins (Condemned / Pit Fighter / Blood)

These are SEARCHED: one plan variant per distinct hand name, crossed with madness cast / decline. So the
buckets only ORDER them: move ordering, plus the depth-0 / beyond-horizon pick. The order is the same
shed order, mapped onto names by first appearance, with one change: **no madness promotion**.

- The Bloodghast-with-a-land leads. While this turn's land drop is open, discard it, play the land,
  and it is back this turn for no mana (hasty at 10 or less).
- The madness-castable creatures do NOT lead, because the outlet's order is built on the PRE-plan board
  and the plan's own casts spend that mana before the trailing pass. The first draft promoted them
  anyway. A d0 probe (seed 1214, game 213) then had Stromkirk Condemned discard Stromkirk Captain on a
  turn whose casts had already spent the mana, so the madness cast could not be paid and the lord was
  lost: T4 -> T5. The searched fan still holds every "discard it and cast it" variant, so nothing
  becomes unfindable.

With the bucket lever on, this replaces the A2 draft's discard tiers. `MTG_VAMP_DRAFT_ORDERS` keeps
governing the sacrifice victims, the trailing activation order and the Bloodghast madness default.

### 3.5 Where it differs from what plays today

- **The free tier is new.** The slivers rule has no notion that a discard can be a cast. At a rollout
  cleanup with spare mana it sheds by its curve fill, which can bin a Baron the mana could have deployed.
- **The lands are counted net of the board to a fixed 4.** The slivers rule derives its targets from
  the deck's dominant creature mana value, which is **3** for this list (12 two-drops tie 12 three-drops,
  and a tie goes up; there are 11 one-drops). So it keeps lands to a hard **4** and a soft **6**, holding
  the 5th and 6th land over parts of its curve fill. The 6 is a slivers number ("2 lords a turn"), not
  this deck's.
- **Castability x value, not a curve fill.** A 4-mana Keeper two lands short sheds before a castable
  2-drop, and the 1/1 Dire Moon sheds before a lord.

## 4. Verification done (2026-10-18)

**Byte-identity, both levers off.** Command: `build/Release/mtg decks/Vampires/Vampire.cod --games 200
--seed 1001 --depth 3 --budget-ms 10 --threads 16`, with `MTG_DUMP_WINS=1`. The pre-change binary (tree
`47bfb0cf0`) and the new one, unset and with both `=0`: **avg 4.4600 / 4.4600 / 4.4600, and all 200
`[win]` lines identical.**

**Unit tests:** `test/unit/test_vampires_order_discard.cpp`, 13 cases. Each lever is forced on through
its heurarm slot, and checked off against VialProvider. `mtg-test` 1527/1527.
- Cast order:
  - the rank lays out section 2.1 exactly and is total over every name, two unknown cards included;
  - 42 permutations of the plan, with and without Vial puts, all realise the same order;
  - Baron casts after the bodies it convokes off;
  - the main-phase view;
  - lever off = VialProvider.
- Buckets:
  - the madness-castable free shed at cleanup (red / white costs the mana cannot pay are not free);
  - the outlet order does NOT promote it;
  - no mana -> a surplus land;
  - Bloodghast with a land ahead of a dead Vial and a surplus land;
  - the outlet order with the land drop open and spent, vs the cleanup pick in the same state;
  - the early / late Vial;
  - castability x value;
  - the held-Vial 1-drop credit;
  - lever off = VialProvider, and the control arm differs.

**Lever liveness, pre-fix binary.** This is NOT the A/B: it is the same seed block as the identity check,
and only one block. Run as one pooled batch, seed 1001. "Changed" = the per-game play digest differs
from `off`.

| cell | games | `order` changed / faster / slower | `buckets` changed / faster / slower | `both` changed / faster / slower |
|---|---|---|---|---|
| d3 b10 | 100 | 54 / 0 / 0 | 9 / 0 / 0 | 59 / 0 / 0 |
| d0 | 400 | 143 / 0 / 0 | 31 / 1 / 2 | 166 / 1 / 2 |

- Both levers are live.
- The order re-sequences about half of all games without moving a win turn. That is expected before
  fix B, because Baron's slot is the only load-bearing one.
- The buckets' d0 net is +1 game-turn in 400. Both slower games are root-caused:
  - **d0 gi 241** (seed 1242, T5 -> T6). T3: two Swamps and no red source, hand Socialite / Captain /
    Pit Fighter, a Vial cast this turn. Condemned's pump sheds Falkenrath Pit Fighter (value 5) and keeps
    Stromkirk Captain (value 11, needs a land AND red). The A2 draft sheds the Captain ("uncastable this
    turn" first). The Vial then puts the Pit Fighter on T4 and the Socialite on T5, and that line wins a
    turn earlier.
    - The held-Vial credit (section 3.2) was added for this game: a held Vial makes a 1-drop castable
      whatever its colour. It raises the Pit Fighter but does not flip this game, because on a real
      library red arrives often enough that the lord's value still wins.
    - What decides this game is speed: the kill comes on T5, before the lord would land. Question 7
      asks whether that should outrank castability x value.
  - **d0 gi 252** (seed 1253, T4 -> T5). Vial at 2 makes both Stromkirk Captain and Captivating Vampire
    (each value 11, mana value 3) puttable next turn. The EV ties, and the NAME sheds Captivating. The
    A2 draft shed the Captain, and the line diverges from there (the T4 greedy plan taps one more body
    for Baron's convoke).
  - Both are d0 greedy-runner games. Analyze-deck 5e accepts d0 churn; the searched cells did not move.
- **The first draft's d0 slower games were fixed, not explained away.** The first draft promoted
  madness-castable creatures at an OUTLET too. It moved d0 gi 213 (seed 1214, T4 -> T5): the pump shed
  Stromkirk Captain on a turn whose casts had already spent the mana. That promotion is now
  cleanup-only (section 3.4). gi 213 and gi 238 no longer move. gi 252 (above) moved only after the
  change, and gi 309 got faster both times.

**Earliest-win mining probe** (5g, the ORDER-rule oracle). Settings: `MTG_DUMP_EWINS=1
MTG_SEARCH_ORDER=1`, seed 61000, 100 games, d5 b3000, every turn, pre-fix binary. Raw dump:
`build/scratch/vampires-order/vamp.ewins.jsonl`, analysed by `scripts/analyze_earliest_wins.py`.
- **ORDER rules: none with enough support** over 441 decisions (turns 1-6). That is 98 of the 100 games:
  I stopped my own probe in its two-game tail after 14 minutes. The earliest-win lines of the
  full ordering search agree on no ordered pair of cards. That is what section 2.1 claims: the deck is
  order-free apart from Baron's convoke. Baron's convoke cannot show in this probe until fix B lands,
  since same-plan bodies are ignored in every order.
- **INCLUSION:** casting Stromkirk Captain (-0.11) or Legion Lieutenant (-0.11) this turn helps. Every
  other card is neutral to slightly slower early, with no "help" cases for the 1/1s, the Vial, Mavren or
  Drana. None of this is an order question. It agrees with the bucket values: lords high, Dire Moon low,
  the Vial an early-only keep.
- **LAND:** the earliest-win lines play Swamp 1922x, Voldaren Estate 1076, Secluded Courtyard 628,
  Unclaimed Territory 612, Blackcleave Cliffs 188. Per copy that is the least flexible land first, a
  drop-order fact rather than a keep fact. Cliffs is last per copy (it enters tapped once 3 lands are
  out), matching its place in the MANA bucket.

## 5. The A/B (NOT RUN: it waits for the convoke / payer fixes)

`test/tools/vampires_order_ab.py` writes ONE pooled manifest. Every arm is a per-job `flags` block, so all
arms share one queue and one tail (CLAUDE.md "WAVES ARE A LOOP").

- **Arms:** `off` (both 0: today's play), `order`, `buckets`, `both`. Each job pins BOTH levers
  explicitly.
- **Cells:** the deck's play settings, std d3 b10 and d5 b20, plus 2HG (`starting_life` 30,
  `opponent_heads` 2) at d3 b10 and d5 b20. `--with-d0` adds the greedy runner (cheap; it reads a rank
  most directly).
- **Seeds:** two held-out blocks, A = 12,180,001 and B = 12,480,001. They are disjoint from smoke /
  regression / overnight (1001-10010) and from the earlier cast-order A/Bs (11,700,001+).
- **Size at scale 1:** per block per arm, 1500 / 750 / 500 / 250 games. That is 24,000 games, about
  10-15 core-hours, about 30-45 min at 20 threads. (d3 b10 measured 1.2 core-s/game on the pre-fix
  binary. Re-project off the first heartbeat.) `--scale` resizes every cell.

```
python3 test/tools/vampires_order_ab.py manifest > build/scratch/vampires-order-ab/manifest.json
MTG_PGO=0 build/Release/mtg --batch build/scratch/vampires-order-ab/manifest.json --threads 20 \
    --game-log-dir build/scratch/vampires-order-ab/wins \
    2> build/scratch/vampires-order-ab/batch.err | tee build/scratch/vampires-order-ab/batch.log
python3 test/tools/vampires_order_ab.py report build/scratch/vampires-order-ab/wins
```

(`MTG_PGO=0` because the arms differ only by flags, so the PGO binary would be equally valid. Pin it
anyway so the per-arm wall numbers compare.) Check the `[batch] heartbeat` utilisation line in the first
ten minutes.

**The report** pairs each arm with `off` per game (same cell, block and global game index). Per cell and
per block it gives faster / slower / net / mean / se / t / changed digests. It lists every SLOWER game with
its single-game repro.

**The bar:**
- every slower game recovers at `--depth 8 --budget-ms 0`;
- every cell's aggregate is net <= 0;
- the sign holds in both blocks;
- the `off` arm equals the committed tree game for game.

Then the result goes to you. The decision on each lever is yours, as is the main split.

**Expected, so a surprise is visible:**
- `order`: a pure re-sequencing. Only Baron's slot can move a win turn, and only after defect B.
  Elsewhere expect changed digests at the same win turn.
- `buckets`: moves the rollouts' cleanup and the outlet move ordering. Expect a broad digest change and
  a small turn effect.
- `both`: the sum of the two, unless the two levers interact.

**Optional supporting probe** (5g; after the fixes): the section 4 probe at the skill's size,
`GAMES=500 SEEDS="61000 62000" scripts/mine_heuristics.sh decks/Vampires/Vampire.cod
decks/Vampires/Vampire.profile.json`. It is the one place an ORDER rule involving Markov Baron could
appear once convoke counts same-plan bodies. Any Baron rule mined BEFORE fix B is a convoke artifact.

## 6. Questions for you (each has the default I implemented)

1. **The cast order in 2.1. Adopt as drafted?** In particular: Socialite and Gorger first; the three
   static lords next; the order-free bodies cheapest first by name; Baron after every body; the Vial
   last. *[Default: as drafted, OFF until you say and the A/B clears.]*
2. **The main split in 2.3: in play or view only?** Turning it on is `ClassifiesMainPhases` (main 1 never
   offers Socialite / Pit Fighter; a lossy narrowing that needs an A/B). Stromkirk Captain on a Drana
   turn: main 1 (+1 this combat) or searched both? *[Default: view only; Captain searched in both mains.]*
3. **The land quota: 4 counting the board, fixed?** Or 3 plus a soft 4th while a Keeper (or two 3-drops)
   is held, the Knights shape? *[Default: 4.]*
4. **The FREE tier: does a madness-castable creature always shed first, even a lord you would rather
   hard-cast next turn?** It is deployed a turn early, so I could not find a case where keeping it is
   better. *[Default: yes, best value first.]*
5. **The Vial: keep one only through turn 2?** Soldiers keeps it through turn 3; slivers only turn 1.
   *[Default: turn <= 2.]*
6. **The value list in 3.2**, especially Dire Moon (2) and Bloodghast (0) as the least-kept, and Keeper
   (12) at the top when castable. *[Default: as listed.]*
7. **Inside the threats, "can it be cast before the kill turn" vs castability x value.** The A2 draft
   sheds whatever the current mana cannot cast this turn first. The buckets weigh a lord's value against
   its distance over four turns. d0 gi 241 (section 4) is the case where the draft's rule wins, because
   the game ends on T5, before the lord would land. Option: drop a card's castability to near zero beyond
   an estimated kill turn (the turn the board's damage clock reaches the opponent's life).
   *[Default: castability x value as drafted. I would try the kill-turn cut as an amendment in the same
   A/B if you want it.]*

## 7. Implementation notes

- **Code:** `VampiresProvider::CastOrderRank / CastOrderTierName / CastOrderTotalByName /
  MainPhaseOverride / CleanupDiscardCandidates / CleanupDiscardShedStable`, and the bucket branch at the
  top of `ActivationDiscardCandidates` (`src/ai/DecisionProviders.cpp`, "VampiresProvider: the
  AI-DRAFTED ..."). The shared shed builder is `VampBucketShedOrder(state, activation_mode)`. Every
  hook's OFF branch returns the VialProvider answer verbatim.
- **Flags:** `EnvOn("MTG_VAMPIRES_ORDER")` / `EnvOn("MTG_VAMPIRES_BUCKET_DISCARD")`, each read through
  `heurarm::Flag`. Two slots are appended at the END of `src/ai/HeuristicArm.h`'s enum and name table.
  That is the one edit outside the provider: a pooled manifest cannot set an env-only lever per job.
- **Params used:**
  - `opp_lost_life_other_enters_counters` / `etb_each_other_own_creature_counters_if_opp_lost_life`:
    Socialite.
  - `grants_madness_creature_subtype`: Gorger.
  - `subtypes_affected` + `power_bonus`/`tough_bonus`: the lords, with `grants_first_strike` (Captain),
    `steal_tap_count` (Captivating) and `convoke` (Baron, tested first).
  - `upkeep_adds_charge`: Vial.
  - `activate_only_if_opp_lost_life`: Pit Fighter's m2 view.
  - `attack_one_or_more_tokens`: Mavren.
  - `landfall_return_self_from_gy`: Bloodghast.
  - `madness_cost` and the granter, through `MadnessCostsFor`: the free tier.
