# Burn — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

## USER-revised design (2026-10-09) -- SUPERSEDES the proposal below

The USER reviewed this proposal on 2026-10-09, one deck at a time, and amended it. Everything in
this section is the review record: USER rulings are quoted verbatim and marked USER; choices an
agent made where the USER had not ruled are marked "agent default" / "agent reading" and are
surfaced for the USER to overturn. Where this section and the proposal below disagree, THIS
section wins. The `#### <deck>: SETTLED` block is the design to implement; the lines above it
record how the review got there (proposal summary, questions, answers).

Implementation contract (all decks): a per-deck `XProvider::CleanupDiscardCandidates` override
returning the full shed order (most expendable first, every non-staged hand card named), routed
through `CleanupDiscardRankingWithOrder`, behind a default-ON `EnvOn("MTG_<DECK>_BUCKET_DISCARD",
true)`; `=0` restores `GenericProvider::CleanupDiscardCandidates` (max-MV). Quotas count the board
("net of board"); within a bucket, distance to castable orders the cards (EV = P(play) x value).

### burn: SETTLED (USER, 2026-10-09)
- "Early" for keeping a single Eidolon = turn 3 or earlier (USER). A not-needed single Eidolon is
  the FIRST spare creature shed (USER).
- SHED: 2nd Eidolon (in play or 2nd in hand) -> Blaze/Blood with no opponent creature (Blaze first)
  -> lands beyond quota -> Shard Volley with no spare land -> burn beyond lethal -> 2nd Light Up the
  Stage -> spare creatures (a not-needed Eidolon first) -> protected.
- SHARD VOLLEY (USER): "One land per volley is okay. I would rank it below all of the other
  bolt-like cards, but still solid." -> each Volley needs its own land among those in play + in
  hand; the "+1 land quota per Volley" and the stricter allowance are DROPPED.
- Burn order: Bolt > Skullcrack > Blaze (creature + land in hand) > Blood that kills > Shard Volley
  > Blaze with no land (1 dmg). Creatures: Goblin Guide > Swiftspear > Eidolon, quota 2 counting board.
- Land quota: 3 counting board, +1 while holding Searing Blaze.
  Light Up the Stage: 1, only with >= 2 lands across board + hand.

(detail log:)
- Land quota 3 counting board (+1 while holding Searing Blaze, +1 per Shard Volley kept) (USER: 3).
- Dead burn = Blaze/Blood with no opponent creature ON THE BOARD NOW; no look-ahead at the spawn
  schedule (USER).
- EIDOLON (USER): "Duplicates should definitely be unprotected. Similarly late game Eidolons could
  also be removed. But, I would keep 1 if we don't have enough creatures and it is relatively early.
  Eidolon is a consistent source of 2 damage per turn and the damage is irrelevant with only one."
  -> a 2nd Eidolon (one in play, or a 2nd copy in hand) is shed first; a single Eidolon is kept only
  while the creature quota (2, counting board) is short AND it is early; otherwise it is a spare
  creature (first spare creature shed). Goblin Guide / Swiftspear stay protected.
  IMPLEMENTATION CHECK: Eidolon is in burn.profile.json mulligan.required_pieces, which the engine
  protects ahead of the provider. Find whether removing it changes MULLIGAN play before touching it;
  tell the user first.
- Open: definition of "relatively early".
- Side note for the user: Blaze/Blood are 8 of 36 spells and the worst learned cards -> a
  deck-screening question, not discard.


#### Agent defaults across the 2026-10-09 review (all decks; surfaced, USER may overturn)

- Creature Giving leftovers: 2nd Suture Priest > Massacre Wurm > 2nd fuel.
- CritterLifegain: Ocelot Prides exempt from the variety rule.
- Dragonstorm: Lotus Bloom after turn 1 sheds right after the 2nd Dragonstorm.
- Giants: Sovereign's "promote with 2+ Giants in play" clause dropped with its demotion.
- Melira Pod: an extra Chord (all pieces present) sits just above the toolbox; "easily castable" =
  every hand piece castable with the mana in play + one land drop.
- slivers_vial: curve window 3 turns.
- Fungus: "foreseeable future" = 5 mana within 2 turns; Beastmaster gives way before the core.
- Selesnya: shed order between lifegain-without-payoff and unreachable payoffs (agent ordering).

#### Implementation notes (2026-10-09)

Implemented as `BurnProvider::CleanupDiscardCandidates` (+ `CleanupDiscardProtectionWaived`,
`CleanupDiscardShedStable`) in `src/ai/DecisionProviders.cpp`, behind `MTG_BURN_BUCKET_DISCARD`
(heurarm `BURN_BUCKET_DISCARD`, default ON). `=0` is `GenericProvider::CleanupDiscardCandidates` with
the waiver OFF, i.e. today's ranking byte-for-byte. Tests: `test/unit/test_burn_discard.cpp`.

- **Shed order emitted:** S1 2nd Eidolon (every hand copy while one is on our battlefield, else every
  hand copy but the first) > S2 dead Blaze, then dead Blood > S3 lands beyond 3 (+1 with a live
  Blaze), counting the board > S4 a Shard Volley without its own land (the k-th Volley needs k lands
  among board + kept hand lands) > S5 burn beyond lethal, worst first > S6 the 2nd Light Up the Stage
  (every copy below 2 lands across board + hand) > S7 spare creatures, a not-needed Eidolon first >
  S8 unrecognised cards > the keep ladder backwards > the kept creatures.
- **Burn order:** Bolt > Skullcrack > Blaze (opponent creature + a kept land in hand, 3) > Blood that
  kills (3) > Shard Volley > Blaze with no land (1) > Blood that cannot kill (0). Creatures: haste,
  then power, the self-taxing body last = Goblin Guide > Swiftspear > Eidolon, quota 2 counting board.
- **Eidolon protection (the profile is NOT edited):** `burn.profile.json` is what play loads for
  `mulligan.required_pieces` (the keep-model sidecar contributes only its `exhaustive_keep` block; it
  holds the same three names), scope `All`. `CleanupDiscardProtectionWaived` waives Eidolon's discard
  protection unless the USER's keep condition holds: no Eidolon on our battlefield, it is the only
  Eidolon in hand, turn <= 3, and board creatures + the hand's Goblin Guides / Swiftspears < 2.
  Goblin Guide / Swiftspear are never waived. The mulligan's use of `required_pieces` is untouched.
- **Other forced-discard paths that read the protection, for this deck:** only the cleanup shed
  (executor `AIEngine::ChooseDiscard` incl. its searched trial set, the rollout `CleanupDiscardShed`,
  the search's discard node) -- all through this ranking, so the waiver applies there. The
  `MTG_DISCARD_TRACE` `prot=` field reads it too (diagnostic). `LandsEdgePitchOrder` is Land's Edge
  only (not in this deck; lands are never required pieces). `ChooseNonCleanupDiscardIndex` passes no
  required pieces at all and no burn card discards outside cleanup. So the waiver changes the cleanup
  shed and nothing else.

Agent readings (the USER may overturn any of these):
1. "Creature quota short" = board creatures + hand Goblin Guides / Swiftspears < 2 (they fill the
   quota before Eidolon, per the creature order). Staged cards are not counted.
2. The waiver reads "the only Eidolon in hand" literally: with two copies in hand both are
   unprotected; the 2nd sheds in S1 and the first, when needed, is placed last -- where the
   protection puts it once the 2nd is gone, so the order does not move.
3. "Early" is read on the turn of the cleanup (`turn_number <= 3`).
4. "Burn beyond lethal" = the opponent's life minus our creatures' next-turn swing (printed power +
   counters, until-end-of-turn pumps excluded), walking the kept burn best-first (the proposal's
   REACH quota; not restated in the USER block). Searing Blood with a creature it cannot kill delivers
   no face damage and ranks below the 1-damage Blaze (the USER order does not place it).
5. The +1 land for Searing Blaze counts only a LIVE Blaze: a dead one is shed in S2, ahead of the
   lands, so it is no longer held when S3 is decided. Blaze's "land in hand" and the Volley land count
   read the KEPT hand lands for the same reason.
6. The kept cards are interleaved by the proposal's section-5 ladder with its clock slots dropped
   (every kept creature is protected): land, land, burn, land, Light Up the Stage, burn, burn, land
   (the Blaze +1), then the remaining burn. Surplus lands shed last-in-hand first (all Mountains).
7. Unrecognised cards (none on this list) shed after the spare creatures, ahead of the keep tail.
8. The bucket arm declares `CleanupDiscardShedStable() == false` (a shed can move the Blaze land, the
   Volley land count or the lethal walk), so a multi-card rollout cleanup is re-ranked per shed --
   what the executor's per-card cleanup already does. `=0` keeps the default `true`.

Observation, not changed: with 4+ lands already in play the literal quota ("3 counting board, +1 while
holding Searing Blaze") makes every hand land surplus, so a held Blaze then has no landfall land and
ranks as the 1-damage Blaze. `PreferHoldLandDrop` banks drops from 3 lands, so this is rare.


#### USER rulings (2026-10-10) on the implementation readings

Cross-deck (verbatim):
- *"Colour beats the land count is correct."* -- a land that gives a KEPT spell a colour nothing on the
  board or already kept provides is kept even past the land quota (Creature Giving, Melira Pod and
  EldraziDisplacerFlicker already do this; every deck follows it).
- *"Creature quotas are less worried about the board than land quotas. This is because you can always add
  more creatures and sometimes this speeds up the clock."* -- creature / threat / payoff quotas are filled
  from the hand WITHOUT subtracting creatures already in play. Land and mana quotas still count the board;
  engine, combo-piece and duplicate-legend rules keep the board rules their design states.
- *"For Distance to castable I think it makes sense to have a smarter model if we can."* -- the shared
  model in docs/design/discard-distance-model.md replaces the per-deck crude proxies.

#### Amendment ADOPTED by the USER (2026-10-13)

`MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE`, default ON: while S4 holds a Shard Volley with no land of its own, a
live Searing Blaze that would deal only 1 (no kept hand land) is shed ahead of it, in S4's slot. USER: *"The
burn one seems okay."* Measurement and the named game (d3 s6105084 gi 83): docs/design/final-ab-2026-10-13.md.

#### Shard Volley when land-light (2026-10-14) -- PROPOSED variants, default OFF, the USER decides

**The finding (2026-10-13 d0 A/B, docs/design/final-ab-2026-10-13.md).** Over 80,000 d0 games the design
was 8 games slower than the old generic (max-MV) discard; the adopted Blaze amendment fixed 3. The other 5
(seed 9310001 + gi: gi 1048, 8292, 12620, 17376, 20102) are hands with NO land on board or in hand, where S4
sheds every Volley ("the k-th Volley needs k lands") ahead of an unkept Light Up the Stage, a spare Eidolon
or a 2nd Skullcrack.

**USER (2026-10-14, verbatim):** *"Yeah, that rule is probably too aggressive when we are land-light. At
least 1 could always be kept and 2 possibly. I think we can give some wiggle room. To be clear, the original
rule here was authored by an agent and I kind of half-heartedly agreed it might make sense, but seeing this in
action and thinking about it we need to be less aggressive. However, there are still many cards Shard Volley
does lose to in these cases. It could be second ranking, 'shard volley with no land' that loses to more of
the potential options."*

**The variants** (heurarm slots, `EnvOn`, all default OFF; with all off the ranking is the committed tree's
byte-for-byte). `T` = lands on board + kept hand lands; the design lets the first `T` Volleys (burn order)
hold a land and sheds the rest in S4.

| slot | what it does |
|---|---|
| `MTG_BURN_VOLLEY_KEEP1` (K1) | the first land-less Volley is not an S4 shed: `T + 1` Volleys are ordinary burn |
| `MTG_BURN_VOLLEY_KEEP2` (K2) | the first two: `T + 2` (overrides K1) |
| `MTG_BURN_VOLLEY_KEEP_FLOOR` (F) | K1 / K2 read as a FLOOR instead: `max(T, K)` -- the literal "at least 1 could always be kept". Identical to the slack reading with no land at all (all five named hands); from `T >= K` on it is the design |
| `MTG_BURN_VOLLEY_NOLAND_RANK` (RANK) | no Volley is ever an S4 shed; a land-less Volley takes the burn-order rank "Shard Volley with no land" (placement below) |
| `MTG_BURN_VOLLEY_NOLAND_LATE` (LATE) | the land-less Volleys (past K1 / K2) keep a shed slot of their own, moved from S4 to just after S6 |

**Where "Shard Volley with no land" sits (RANK), and why.** Burn order: Bolt > Skullcrack > Blaze (creature
+ kept land, 3) > Blood that kills (3) > Shard Volley > **Shard Volley with no land** > Blaze with no land (1)
> Blood that cannot kill (0).
* Below the with-land Volley, Blaze-with-land and Blood-that-kills: by definition (it is the same card one
  land short), and those already outrank a Volley.
* ABOVE Blaze-with-no-land: that is the USER-adopted amendment (2026-10-13, `MTG_BURN_VOLLEY_KEEP_OVER_DRY_BLAZE`:
  "a 1-damage Blaze sheds ahead of a land-less Volley"). The card reason: a land-less Volley is 3 damage for
  {R} the turn one more land arrives, and on the kill turn its sacrifice is free -- the Mountain that pays the
  {R} can be the land sacrificed (CR 601.2g-h: mana abilities are activated before costs are paid) -- while
  the dry Blaze is {R}{R} for 1 and needs an opponent creature.
* ABOVE the Blood that cannot kill (0 face), the unkept Light Up the Stage (S6: a 2nd copy, or any copy below
  2 lands) and a spare creature (S7: in practice a not-needed Eidolon; Goblin Guide / Swiftspear are
  protected): the design already sheds all three ahead of the 1-damage Blaze, so above it is forced by the
  amendment. The cards agree: below 2 lands Light Up the Stage costs {2}{R} (Spectacle {R} needs damage
  dealt first, so two mana in practice) and a 2nd copy overlaps the first (staged until the end of the next
  turn); a not-needed Eidolon is a {R}{R} 2/2 that deals us 2 for every spell we cast. And so do the losing
  games: three of the five (12620, 17376, 1048 T3) are a Volley shed ahead of an unkept Light Up the Stage,
  two (8292 T3, 1048 T4) ahead of a spare Eidolon.
* **Consequence:** nothing on this list sorts between two Shard Volleys, so RANK is order-identical to never
  shedding a Volley for want of a land (K = infinity). K1+RANK and K2+RANK are therefore provably identical
  to RANK (no Volley is left to reach S4) and are not measured; the code makes RANK supersede the others and
  a unit test pins it.
* **LATE is the "loses to more of the potential options" reading, measured so the USER can compare.** The
  land-less Volleys keep a slot of their own, after S5 (burn beyond lethal) and S6 (the unkept Light Up the
  Stage), still ahead of a spare creature, the 0-face Blood and every kept card. It breaks the transitivity
  above (spare Eidolon < dry Blaze < land-less Volley < spare Eidolon), which is why it is not my placement.
  The amendment still holds under it: a KEPT 1-damage Blaze moves up to just ahead of the late Volleys; one
  already beyond lethal (S5) is ahead of them where it is.

**Measured (2026-10-14).** ONE pooled paired batch, commit 08d36471 (src tree 6f0a1111, on 77e094a2),
1,700,000 games: 10 arms x {shipped (value_play d6, b20) 4,000; d3 b10 6,000; d0 160,000} games each.
Seeds: the 2026-10-13 A/B's (ship 9300001+, d3 9305001+, d0 9310001+; seed = base + gi, game_index = gi)
plus fresh held-out ones (ship 10200001+, d3 10210001+, d0 10220001+), 50/50 at ship and d0, 4,000 + 2,000
at d3. Loss = 9, paired per game. "generic" = `MTG_BURN_BUCKET_DISCARD=0`; "cur" = the design + the adopted
Blaze amendment, i.e. the committed tree (the arm with every new slot off is byte-identical to the
pre-change binary: probe digests equal).

| arm | ship 4,000 vs cur | d3 6,000 vs cur | d0 160,000 vs cur (old / new) | d0 vs generic |
|---|---|---|---|---|
| cur (the design) | -- | -- | -- | 1 faster / 14 slower, **+13** |
| K1 | 0 / 0 | 0 / 0 | **8 / 0, -8** (3 / 5) | 2 / 7, +5 |
| K2 | 0 / 0 | 0 / 0 | **12 / 0, -12** (5 / 7) | 2 / 3, +1 |
| F1 (K1 as a floor) | 0 / 0 | 0 / 0 | 8 / 0, -8 -- the same games as K1 | 2 / 7, +5 |
| F2 (K2 as a floor) | 0 / 0 | 0 / 0 | 12 / 0, -12 -- the same games as K2 | 2 / 3, +1 |
| RANK | 0 / 0 | 0 / 0 | **12 / 0, -12** -- the same games as K2 | 2 / 3, +1 |
| LATE | 0 / 0 | 0 / 0 | 8 / 0, -8 (2 / 6) | 1 / 6, +5 |
| K1+LATE | 0 / 0 | 0 / 0 | 9 / 0, -9 (3 / 6) | 2 / 6, +4 |
| K2+LATE | 0 / 0 | 0 / 0 | 12 / 0, -12 -- the same games as K2 | 2 / 3, +1 |

(faster / slower vs the reference, net = sum of turn differences.) At the shipped setting every arm's PLAY
is byte-identical to cur (0 of 160 job digests differ; the generic differs in 1 chunk, no win turn); at d3
the arms change play in 3-4 of 120 chunks and no win turn. At d0 the floor and slack readings change play in
9 (K1 / F1) and 4 (K2 / F2) of 320 chunks but never a win turn, so the data do not choose between the two
readings of "at least 1 could always be kept".

**No arm has a slower game than cur in any cell (170,000 paired games per arm), so the slower-game recovery
bar is vacuous.** For information, every candidate's games still slower than the GENERIC discard were
replayed at stage 1 (`--depth W --budget-ms 100`, W = generic's turn) and stage 2 (`--depth 8 --budget-ms 0`)
-- see "the residual" below.

**The five named games** (d0, seed 9310001 + gi; win turn, 9 = unwon):

| gi | generic | cur | K1 / F1 | K2 / F2 / RANK / K2+LATE | LATE / K1+LATE | what the winning arms do |
|---|---|---|---|---|---|---|
| 1048 | 8 | 9 | 9 | **8** | 9 | T3 (no land, 2 Volleys) shed the unkept Light Up the Stage instead of a Volley; T4 the spare Eidolon (T4 > 3) |
| 8292 | 7 | 8 | 8 | **7** | 8 | T3 (no land, 2 Volleys) shed the spare Eidolon (creature quota met) instead of a Volley |
| 12620 | 6 | 7 | **6** | **6** | **6** | T2 (no land, 1 Volley) shed the 2nd Light Up the Stage instead of the Volley |
| 17376 | 5 | 6 | **5** | **5** | **5** | T2 (no land, 1 Volley) shed a Light Up the Stage instead of the Volley |
| 20102 | 4 | 5 | 5 | 5 | 5 | every arm sheds the Volley ahead of the 2nd Skullcrack -- the USER burn order (below) |

All 16 games any arm moves (d0; nothing moves at ship or d3): faster under EVERY arm: 12620, 17376 (old),
5762, 25618, 31448, 33514, 77776 (new). Faster under K2 / F2 / RANK / K2+LATE but not K1 / F1: 1048, 8292
(old), 33644, 36730 (new) -- the 2nd land-less Volley. LATE misses 1048, 8292, 36730 and 72260 (old; cur =
generic = 7, every other arm 6): in each it sheds a land-less Volley ahead of a SPARE EIDOLON, which is exactly
the extra option LATE makes the Volley lose to -- 4 games, 0 the other way. 76294 (new) is the one game where
cur beats the generic (6 vs 7); every arm keeps it.

**The residual: three games every arm still loses to the generic discard (20102, 28164, 74468), and they are
not S4.** All three are hands with no land anywhere, where the Volley is the LAST card of the USER's burn
order (Bolt > Skullcrack > ... > Shard Volley) and is shed ahead of a 2nd Skullcrack; the generic sheds the
Skullcrack (MV 2) and on the kill turn casts Bolt + Volley off two lands (1 mana each, the Volley's land
sacrificed after it tapped) where Skullcrack alone costs 2. No variant here touches the burn order, so they
stay. Recovery (vs the generic's turn) for the K2 / RANK arm: 28164 recovers at stage 1 (d6 b100: T6); 20102
and 74468 do not, at stage 1 or at stage 2 (d8 b0: T5 and T7 -- the same turns cur and every other arm reach
there). Across all arms 21 of 52 slower-than-generic games recover at stage 1 and none of the other 31 at
stage 2; none of them is slower than cur.

Summaries: `logs/burn_volley/` in the agent worktree `.claude/worktrees/agent-ad3abe568de212302` (analysis.txt, the recovery listings, the named-game
discards); scripts `scripts/burn_volley_ab/` (make_ab.py builds / analyses the batch, make_recovery.py the two
stages, named_games.sh + named_summary.py the per-arm discards of named games).

**Agent recommendation (the USER decides):** RANK, or K2 -- they measure the same (12 faster / 0 slower at d0
over 160,000 games, nothing moves at ship or d3, every slower-than-generic game they leave is the burn order's
Skullcrack-over-Volley). RANK is the simpler rule: it removes S4 outright (the agent-authored rule the USER
"half-heartedly agreed" to) and gives the land-less Volley a burn-order rank; K2 keeps S4 for a 3rd land-less
Volley and never measured different from RANK. K1 / F1 leave 4 games on the table (the 2nd land-less Volley:
1048, 8292, 33644, 36730). LATE, the "loses to more of the potential options" reading, loses the 4 spare-Eidolon
games against RANK and gains none. The slack-vs-floor reading never changed a win turn.

**Questions for the USER (surfaced, not blocking):**
1. Which arm, if any (agent default: none adopted; all ship OFF).
2. "At least 1 could always be kept and 2 possibly": a floor (`max(lands, K)`) or one/two Volleys of slack past
   the lands (`lands + K`)? Measured identical on win turns; implemented both.
3. The residual 20102 / 28164 / 74468 are the burn ORDER, not S4: with no land anywhere a 2nd Skullcrack ({1}{R})
   outranks a Shard Volley ({R}, its land free on the kill turn). No variant touches the USER's order.

---

## The original proposal (2026-09-23, AI-authored; superseded where the section above differs)


Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 sweep. **Deliverable
is this document; no `.cpp`/`.h` file was touched.** Integration is central, afterwards.

Target: `BurnProvider::CleanupDiscardCandidates`, behind a default-on
`EnvOn("MTG_BURN_BUCKET_DISCARD", true)` with `=0` restoring
`GenericProvider::CleanupDiscardCandidates`. Decklist `decks/burn/burn.txt` (there is no `.cod`).

Unlike Minotaur, shipping this **promotes nothing**: `BurnProvider` already exists
(`src/ai/DecisionProviders.h:460`) and the deck is already routed to it by
`if (p.landfall_damage > 0) { burn = true; }` (`src/ai/DecisionProviders.cpp:10103`). This adds one
override to a live provider that today carries only `PreferHoldLandDrop` and
`HoldsSacLandBurnUntilLethal`.

---

## 0. What I verified, and what I did NOT run

**Rule 0 honoured.** Every one of the deck's ten distinct cards was read from
`src/cards/data/cards.json` — mana cost, types, keywords, P/T, `oracle_text`, `parameters`. Nothing
below is from recall. I additionally verified, in the engine, the six facts this policy leans on:

| claim | where |
|---|---|
| a creature-targeting burn is **uncastable** with no opponent creature (bar the prowess line) | `TurnSolver.cpp:13877-13896`, `FindOwnProwessBurnTarget` `TurnSolver.cpp:6084` |
| the goldfish presents creatures on a **10-game cycle**, and 2 of 10 patterns present **none** | `GoldFishRunner::PopulateOpponentSpawns`, `src/runner/GoldFishRunner.cpp:547-575` |
| Eidolon's on-cast trigger damages the **active player** — i.e. us | `FireOnCastTriggers`, `src/core/SpellEffects.h:2897-2900` |
| our own life **does** gate a win (self-kill guards) | `AIEngine.cpp:2020`, `TurnSolver.cpp:40006` |
| staged cards do **not** count toward the hand limit, so Light Up the Stage never causes a shed | `TurnSolver.cpp:29711-29718` |
| `required_pieces` protection is **`DiscardProtectScope::All` by default** and is applied *ahead* of the provider's tier A | `GameState.h:564`, `SpellEffects.h:418-425` |

**UN-RUN: the `MTG_SHED_STATS` census.** The Minotaur doc's central lesson is that the real-play
census is the wrong denominator and only `MTG_SHED_STATS` answers the right question. I did not run
it: the box was at load average 26 on 24 cores throughout (the 16-agent sweep plus other work), and
`box-resource-rules-2026-09-14` is "ONE batch/test at a time". Recording it as **UN-RUN**, not
deferred. The command integration should run first:

```
MTG_SHED_STATS=1 build/Release/mtg decks/burn/burn.txt \
  --profile decks/burn/burn.profile.json --games 200 --seed 1001 --depth 5 --budget-ms 20
```

Everything in §8 D6 that depends on that number is flagged as an expectation, not a finding.

---

## 1. The deck, in one paragraph — and therefore its shape

Burn is 24 Mountain and 36 spells, mono-red, **every nonland card at mana value 1-3**. It needs three
things to function: two or three lands, a hasted body or two on the board early, and a stream of
cheap face damage — and it wins by **REACH**, i.e. arithmetic: total damage delivered versus the
opponent's remaining life. That makes a "threat" fungible here in a way it is not on any other deck
in this sweep: 3 damage is 3 damage regardless of which card carries it.

**But it is not the brief's simple 2-bucket case, and the third role is real.** A burn spell is
**one-shot**; a creature's power **recurs every combat**. Over the deck's measured ~4.3-turn arc a
turn-1 Goblin Guide delivers 6-8 damage for {R} while a Lightning Bolt delivers 3 once, and no
"damage-per-mana" ordering over a single card's printed damage can see that. The engine has already
priced exactly this axis for another deck and deliberately stopped short of collapsing it: the
trained `MTG_GOBLIN_FACE_VALUE` weight sets one point of face damage equal to one point of *power*
and its comment says so explicitly — *"Burn is one-shot where a body's power recurs every combat, but
against a goldfish it is unconditional... Parity is the claim; it deliberately does NOT assert that
burn beats bodies"* (`DecisionProviders.cpp:7463-7466`). Parity of a *point* of damage with a *point*
of power is not parity of a *card*; a recurring 2 power and a one-shot 3 are different slots, so they
are different buckets.

There is also a fourth, small role the other three do not cover: **Light Up the Stage refuels**. It
delivers zero damage and is not a body; in the only state where this deck ever discards — land screw
— it is the card that fixes the problem. That is the Minotaur Ragemonger ruling applied to a
different deck: a card whose job is to answer the state every shed is taken in cannot be the last
thing kept. So it gets a slot of its own, classified **with the mana**.

**Shape: four buckets — MANA (lands), CLOCK (recurring bodies), REACH (one-shot face damage),
DIG (refuel).** The bucket count comes from the brief's own test ("similar effects grouped; two cards
that fill different slots do not share a bucket"), not from another deck's list.

### Why this deck needs a policy at all — one hand

T3, one Mountain on the battlefield, **no opponent creature** (spawn pattern 0 or 1 — 20% of games),
opponent at 20. Hand of 8: `Mountain, Mountain, Goblin Guide, Goblin Guide, Lightning Bolt,
Searing Blaze, Searing Blood, Light Up the Stage`.

The shared fallback's tier B is **descending mana value** over the non-protected cards. Both Goblin
Guides are `required_pieces` and protected, so the eligible set is
`Mountain(0), Mountain(0), Bolt(1), Blaze(2), Blood(2), Light Up the Stage(3)` — and
`CleanupDiscardManaValue` reads the **printed** cost, not the Spectacle cost. So today's shed is
**Light Up the Stage**: the engine pitches the deck's only refuel, out of a land-screwed hand, while
holding two burn spells that are *uncastable* in this game and will be uncastable in every turn of
it. This policy's index 0 for that hand is **Searing Blaze**.

---

## 2. Card-by-card role table

Every row derived from `cards.json`. `MV` is the printed mana value (what `CleanupDiscardManaValue`
returns).

| n | card | cost | MV | `params` keyed on | bucket |
|---|---|---|---|---|---|
| 24 | Mountain | — | 0 | `produces:[R]`; `CleanupDiscardIsLand` | **MANA** |
| 4 | Lightning Bolt | `{R}` | 1 | `damage:3`, `targeting:any` | **REACH** (unconditional, 3 face) |
| 4 | Shard Volley | `{R}` | 1 | `damage:3`, `targeting:any`, **`sacrifice_land:true`** | **REACH** (3 face, eats a land) |
| 4 | Skullcrack | `{1}{R}` | 2 | `damage:3`, `targeting:player` | **REACH** (unconditional, 3 face) |
| 4 | Searing Blaze | `{R}{R}` | 2 | `damage:1`, `targeting:multi`, **`landfall_damage:3`** | **REACH** (conditional — see below) |
| 4 | Searing Blood | `{R}{R}` | 2 | `damage:2`, `targeting:creature`, **`death_trigger_damage:3`** | **REACH** (conditional — see below) |
| 4 | Light Up the Stage | `{2}{R}` | 3 | `draw:2`, `stages_cards:true`, `spectacle_cost:{R}` | **DIG** |
| 4 | Goblin Guide | `{R}` | 1 | Creature 2/2, `Haste` | **CLOCK** |
| 4 | Monastery Swiftspear | `{R}` | 1 | Creature 1/2, `Haste`, `Prowess` | **CLOCK** |
| 4 | Eidolon of the Great Revel | `{R}{R}` | 2 | Enchantment Creature 2/2, **`on_cast_trigger_max_mv:3`, `on_cast_trigger_damage:2`** | **CLOCK** (and a liability — D1) |

### The two genuinely ambiguous cards, and they are ambiguous for the same reason

**Searing Blaze and Searing Blood are 8 of the 36 spells and neither is reliably a damage source.**
`Targeting::Multi` and `Targeting::Creature` both require an **opponent** creature
(`HasLegalCreatureTarget`, `TurnSolver.cpp:6070`), and with none the enumeration `continue`s — the
spell is not offered at all — unless `FindOwnProwessBurnTarget` finds a prowess attacker *and* an own
creature that survives the burn. Three consequences, all verified:

* **Searing Blood with no opponent creature is simply uncastable.** Its creature damage is 2, and no
  creature in this deck has effective toughness > 2 un-pumped (Swiftspear 1/2, Goblin Guide 2/2,
  Eidolon 2/2), so `FindSurvivingOwnCreature(…, 2)` returns -1. It comes back only after a prowess
  trigger has already pumped a Swiftspear to 2/3 this turn.
* **Searing Blaze's prowess self-cast delivers ZERO face damage to the opponent.** The Multi half
  hits `tgt_ctrl` — the *target's* controller — so a self-cast takes 1 off **our** life
  (`TurnSolver.cpp:26770-26774`). Its whole content in that line is +1/+1 to prowess creatures.
* **20% of games never present a creature.** `PopulateOpponentSpawns` keys the pattern on
  `game_index % 10`, and patterns 0 and 1 are `{}`. In those games both cards are dead all game.

The deck's own learned `card_scores` agree emphatically and independently: Searing Blaze **-0.554**
and Searing Blood **-0.537** are the two worst first-copy marginals in the file, and Blaze's second
copy at **-0.740** is the worst number anywhere in it. (Read with the D5 staleness caveat — cited as
corroborating *direction*, never as the order.)

I classify both as **REACH**, not as a separate "removal" bucket, because the census resolves it
without a bucket: their contribution to the deck's only win condition is a number that the engine
already computes, and when that number is 0 they are overflow by arithmetic. See §4.

---

## 3. The buckets, with quotas — each stated net of board

The board census runs **before any quota** (brief rule 2). Count, over
`p.controller_index == s.active_player_index`:

```
board_lands      p.card.IsLand()
board_power      sum of p.card power over our creatures that could attack   (ONE swing; see §7)
board_clock      count of our creatures
opp_creature     the opponent side's creatures (via FindBurnKillTarget, which does the work)
```

and over the hand (non-staged cards only):

```
live_hand_lands  count of is_land(i) in hand.  For THIS deck it is simply the Mountain count --
                 every land is a basic Mountain, so unlike Minotaur's Karoos or Dragons' Gruul Turf
                 there is no "a land that is not really a land" case to net out. Stated as a
                 separate quantity anyway so a screening arm that adds a utility land has an
                 obvious place to narrow it.
```

### 1 — MANA (lands). Quota 3 net of board, plus two deck-specific riders.

```
land_need = max(0, kLandTarget - board_lands) + volley_allowance + landfall_bonus
kLandTarget = 3
```

`kLandTarget = 3` is the deck's **own** number, not an invention: `BurnProvider::PreferHoldLandDrop`
already banks the land drop at `kBankThreshold = 3` with the comment *"Burn's curve tops out at mana
value 2, so ~2-3 lands cast the whole deck"* (`DecisionProviders.cpp:5281-5288`). Using the same
constant in both places is the point — a discard rule that wanted more lands than the land-drop rule
will develop would fight it. (Whether 3 or 4 is right is D3.)

**`landfall_bonus` = 1 when the hand holds a `landfall_damage > 0` card.** This is the coupling the
brief asked to be called out: **a land is not purely a mana card in this deck.** Searing Blaze deals
1 to the face, or **3** if a land entered under our control this turn
(`EffectHandler.cpp:548-552`, `SpellEffects.h:2378`). So holding a Blaze makes one land in hand worth
two extra damage — the land IS part of the threat. The land quota therefore rises by one so the drop
is available on the Blaze turn, which is the discard-side twin of what `PreferHoldLandDrop` does at
the play site.

**`volley_allowance` = the number of Shard Volleys the reach bucket is keeping.** Shard Volley
sacrifices a land as an additional cost (`sacrifice_land: true`), so the coupling runs the *other*
way: each Volley we intend to cast consumes one land off the battlefield. This is why the allowance
is cumulative (§4) and why it feeds the land quota rather than being priced only inside reach.

What fills this quota from the battlefield: any land we control. Nothing else — this deck runs no
mana rock, no dork, no Karoo, no fetch. `DiscardLandsFirst` stays at the `GenericProvider` default
`false`: burn has no `discard_land_damage` outlet, so a land in hand is never ammunition.

### 2 — CLOCK (recurring bodies). Quota 2 net of board.

```
clock_need = max(0, 2 - board_clock)
```

Two hasted one-drops deployed on turns 1-2 supply roughly half of 20 life across the deck's ~4.3-turn
arc. A third body is mostly redundant because the binding constraint in the shed state is **mana, not
bodies** — and the deck has twelve of them. A resolved creature fills this quota completely: a Goblin
Guide already attacking needs no hand copy.

**State it honestly: this quota is unreachable today.** All twelve creatures are
`mulligan.required_pieces` (`Goblin Guide`, `Monastery Swiftspear`, `Eidolon of the Great Revel`) and
`GameState::m_discard_protect` defaults to `DiscardProtectScope::All` — *every* copy protected, not
just the last. `CleanupDiscardRankingWithOrder` drops protected indices from the provider's tier A
(`if (prot[i]) { continue; }`), so no clock card this policy names can ever be shed while that holds;
they fall to tier C, behind everything. The quota is specified anyway so the policy is correct if
`required_pieces` is ever narrowed or `MTG_DISCARD_PROTECT=hand` is set. See D1 — this protection is
the largest thing I found, and it is not a discard-ranking problem.

### 3 — REACH (one-shot face damage). Quota = damage still needed, net of board.

```
reach_need_damage = max(0, s.Opponent().life - board_power)
```

Walk the reach bucket best-first, accumulating each card's `ReachFaceForKeeping` (§4); protect cards
until the running total reaches `reach_need_damage`. Everything past that point is overflow.

**Read `s.Opponent().life`; never hardcode 20.** The suite's `burn2hg` cell is *the same deck and
profile at `starting_life 30`* (`test/regression_cases.sh:239,276`), so a life-based quota is
automatically right there and a constant would be wrong in one of burn's six suite cells.

**And state the structural finding plainly: this quota is VACUOUS in the normal case.** At 20 life
with a 2-power board the hand owes 18, and the hand's best realistic contents are four or five
3-damage spells. The bucket is essentially never full, so **quota-first does no work inside REACH and
the within-bucket ORDER is the entire decision.** That is not a defect of the formulation; it is what
"the win condition is arithmetic" actually implies for a deck that cannot hold lethal. The quota does
bind at exactly one moment, and it is a real one: **late, when lethal is already assembled.** With
the opponent at 5 and a Bolt plus a Skullcrack in hand, the sixth burn spell is genuinely free, and
then surplus reach sheds ahead of a surplus land (§7 promotion 4).

### 4 — DIG (refuel). Quota 1, gated on being castable.

```
dig_need = (board_lands + live_hand_lands >= 2) ? 1 : 0
```

Light Up the Stage is `{2}{R}`, or `{R}` under Spectacle once the opponent has lost life this turn
(`ManaPayment.cpp:1329-1331`). At one total source it is dead in both modes — the Spectacle enabler
itself costs the only mana. At two sources the line is live (burn spell, then Spectacle for `{R}`),
and in a land-screwed hand `draw: 2` is the out. So the quota is 1 whenever two sources exist across
board **and** hand, and 0 below that, at which point the dig drops into the overflow shed behind the
surplus lands. That gate **is** this bucket's distance-to-playable term.

A second copy is never quota-protected: `stages_cards` exiles cards playable only until the end of
our next turn, so two copies in one window overlap rather than stack.

---

## 4. The within-bucket order, and the distance term

### REACH — order by face damage delivered, then by mana

The ordering key is **(face damage, descending), then (mana value, ascending)** — damage first,
damage-per-mana as the tie-break. The damage number is computed by a function that mirrors the
engine's own reach credit, so the ranking and the search agree about what a card is worth:

```
int ReachFaceForKeeping(i):                       // 0 == this card cannot contribute; head of the shed
    if (!is_reach(i)) return 0;
    const CardParams& p = def_of(i)->params;
    if (needs_creature(i)) {                      // Targeting::Creature or Targeting::Multi
        int ti = FindBurnKillTarget(s, me, p.damage);      // -1 with NO opponent creature
        if (ti < 0) return 0;                              //   -> DEAD
        if (p.death_trigger_damage > 0)                    // Searing Blood: face only if it KILLS
            return s.battlefield[ti].EffectiveToughness() <= p.damage ? p.death_trigger_damage : 0;
    }
    if (p.landfall_damage > 0)                             // Searing Blaze
        return live_hand_lands > 0 ? p.landfall_damage : p.damage;
    return p.damage;
```

Three notes, each deliberate:

* **`FindBurnKillTarget` does the whole census.** It is in `SpellEffects.h:2354` and is already shared
  in lockstep by the executor, the rollout apply and the value model's reach estimate. Reusing it
  means the live/dead split needs no separate board scan and cannot drift from the cast site.
* **The landfall test is `live_hand_lands > 0`, NOT `ap.lands_played_this_turn > 0`.** The cast-site
  formula (`TurnSolver.cpp:15303`) asks about *this* turn, which is the right question when you are
  about to cast. A discard ranking is deciding what to hold for a *future* turn, so the right question
  is "will a land drop be available when I cast this" — i.e. do we hold a land. Using the cast-site
  test here would rate a held Blaze at 3 on a turn we happened to have already played a land and at 1
  otherwise, which is noise.
* **`damage` values in this deck are 3 for Bolt/Volley/Skullcrack and 3 for a live Blaze/Blood**, so
  the mana tie-break is what actually orders them. This is why the order below is mostly
  cost-ordered — it is derived, not assumed.

Resulting order on this decklist, best kept first:

1. **Lightning Bolt** — 3 face, `{R}`, `targeting:any`, no condition of any kind.
2. **Shard Volley (within the allowance)** — 3 face, `{R}`. **This ranks high, and that is a
   correction to the obvious reading.** The naive view is that eating a land in a screwed hand is
   near-catastrophic; but `BurnProvider::HoldsSacLandBurnUntilLethal()` is already `true` and
   `HoldSacLandBurn` (`TurnSolver.cpp:21367`) prunes every plan that casts it without winning or
   unlocking Spectacle. So the engine never spends the land early: a held Shard Volley is **stored
   reach for the winning turn**, when the land no longer matters. The cast side is settled; the
   discard side must not re-litigate it.
3. **Skullcrack** — 3 face, `{1}{R}`, unconditional but a mana more than the two above.
4. **Searing Blaze, with a creature present and a land in hand** — 3 face.
5. **Searing Blood, with a killable creature** (`EffectiveToughness <= 2`) — 3 face via the rider.
   The spawn table helps here: patterns 2,3,4,5,9 are all 1/1s and 2/2s, so when a creature exists it
   is usually killable.
6. **Searing Blaze, creature present but no land in hand** — 1 face.
7. **Shard Volley beyond the allowance** — see the cumulative rule below.
8. **Searing Blaze / Searing Blood with no opponent creature** — 0 face. Dead; head of the shed.

Among the dead (`ReachFaceForKeeping == 0`), order by the damage the card *would* deliver if a
creature appeared: Blaze is 1-or-3 with a land condition attached, Blood is a flat 3, so **Blaze
sheds before Blood**. (The learned marginals order them the same way, -0.554 before -0.537.)

### The Shard Volley cumulative allowance — the distance term, in LANDS

The k-th `sacrifice_land` burn needs the k-th **spare** land:

```
volley_ok(k) :=  board_lands + live_hand_lands - max(0, kLandTarget - board_lands) >= k
```

This is the shape of Minotaur's adopted `DUPES` rule — the k-th copy's requirement is cumulative, so
`P(play)` collapses on its own and no per-card constant is needed — but denominated in **lands**
rather than mana, because lands are the resource Volley duplicates compete for. It needs no name
test (`p.sacrifice_land`) and it leaves the other burn alone: two Lightning Bolts at two sources are
both castable, two Shard Volleys need two spare lands. The learned marginals corroborate the shape
and nothing else in the deck does: Shard Volley's second copy drops 0.278 → **0.153** while
Skullcrack's holds 0.237 → 0.175, i.e. the second Volley is the one that gets worse.

### CLOCK — order by haste, then power, then prowess, then self-tax

Param-derived, not name-bound:

1. `HasKeyword(Keyword::Haste)` first — a hasted body attacks the turn it lands, which on a 4-5 turn
   clock is a whole extra swing.
2. then **power descending** — Goblin Guide 2 over Monastery Swiftspear 1.
3. then `HasKeyword(Keyword::Prowess)` — a Swiftspear in a hand full of burn is effectively 2-3 power.
4. then **`on_cast_trigger_damage == 0` before `> 0`** — a body that taxes our own spells sorts
   **last** among bodies. This is the param-level expression of D1 that a provider is allowed to make
   on its own; it is inert while all twelve are protected, but it is the correct order.
5. then MV descending.

That yields **Goblin Guide > Monastery Swiftspear > Eidolon of the Great Revel** — which is exactly
the learned order (0.794 > 0.255 > -0.133), arrived at independently from `params`. I take the
agreement as a check on the predicates, not as the source of the order.

### MANA and DIG — no internal order needed

All 24 lands are Mountains: interchangeable, so hand order is a fine total order and there is nothing
to judge. All four dig cards are Light Up the Stage. No Karoo, no fetch, no utility land — none of
Minotaur's or Dragons' land caveats apply to this deck.

---

## 5. The total order over a hand

**Quotas are INTERLEAVED, not filled bucket-by-bucket** — the Minotaur implementation lesson. "Three
lands", "two bodies" and "18 damage" are three constraints on the same seven-card hand, so what the
ranking has to say is which land beats which spell. Keep priority, one card per slot:

```
land1 > clock1 > land2 > reach1 > land3 > dig1 > reach2 > clock2 > reach3 > land4 > reach4 > clock3 > reach5…
```

Read forwards it is the fill order; read **backwards** it is the order the quota-protected cards give
way in, which is what matters most here because a burn hand is usually fully covered by quota.
Justifications for the three placements that are not obvious:

* **`land1` above everything.** At 0-2 lands — and that is the only state in which this deck ever has
  eight cards — a Mountain is the most valuable card in hand, full stop. A missed land drop is the one
  thing burn cannot recover from.
* **`clock1` above `land2`.** A `{R}` 2/2 haste deployed now is the deck's single highest-value card
  and it recurs; the second land arrives 40% of draws.
* **`land4` BELOW `reach3`.** Slots past `land3` exist only when `volley_allowance` or
  `landfall_bonus` raised `land_need`. Putting them below three burn spells says the right thing: the
  extra land those riders ask for is worth less than the damage it is meant to enable.

Shed order returned to `CleanupDiscardRankingWithOrder`, **most expendable first**:

```
S0  REACH that is DEAD by the board census (ReachFaceForKeeping == 0):
      a creature-targeted burn with no opponent creature. Blaze before Blood (see §4).
S1  lands past land_need. (The quota already contains the Volley and landfall riders.)
S2  a Shard Volley beyond volley_ok(k) — a stored 3 damage we have no land to pay for.
S3  REACH past reach_need_damage — rare, and it fires exactly when lethal is already in hand.
S4  surplus DIG: a second Light Up the Stage, or any copy while dig_need == 0 (< 2 sources).
S5  surplus CLOCK past clock_need (unreachable while required_pieces protects all twelve).
S6  anything this policy does not recognise — no opinion is a reason to protect a card, not to pitch
      it. (Empty on this decklist; it exists for a screening arm that introduces a new card.)
KEEP TAIL: the keep priority above, reversed, so the list names EVERY card in the hand.
```

**The keep tail is not optional.** Anything omitted falls through to the shared tier B — descending
mana value — which on this deck pitches Light Up the Stage (MV 3) out of a screwed hand while holding
two uncastable `{R}{R}` spells. That is the Mirrorwing gi295 lesson and the §1 worked hand is its
concrete form here. Index 0 is therefore determined for any hand.

**Worked example (the §1 hand).** Board: 1 Mountain, no creatures, no opponent creature; opponent 20.
Hand: `Mountain, Mountain, Goblin Guide, Goblin Guide, Lightning Bolt, Searing Blaze, Searing Blood,
Light Up the Stage`. Sources = 1 board + 2 hand = 3. `land_need = max(0,3-1) + 0 + 1 = 3`, so both
Mountains are kept and none is surplus. `clock_need = 2` → both Goblin Guides kept (and protected
regardless). `ReachFaceForKeeping`: Bolt 3, Blaze **0**, Blood **0**. `reach_need_damage = 20 - 0`,
never met → Bolt kept. `dig_need = 1` (3 sources ≥ 2) → Light Up the Stage kept. So S0 = `[Searing
Blaze, Searing Blood]` and **index 0 = Searing Blaze**, versus **Light Up the Stage** under the
generic fallback.

`CleanupDiscardShedStable()` stays at the default `true`: the only position-dependent term is the
Volley allowance, and later copies rank worse, so shedding the worst-ranked copy never changes an
earlier card's rank. Integration should confirm with `MTG_DISCARD_SHED_VERIFY=1` on a smoke run
rather than taking my word for it.

---

## 6. Param-level classification predicates

Named concretely so integration is mechanical. Every characteristic read goes through
`CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only placeholder
(`DeckLoader::MakePlaceholder`), so its own type/cost masks are empty. **No predicate below tests a
card name**, which is what keeps a screening arm that swaps a card in the right bucket.

| predicate | expression | bucket / effect |
|---|---|---|
| `is_land(i)` | `CleanupDiscardIsLand(ap.hand[i])` | **MANA** |
| `is_clock(i)` | `d->card.IsCreature()` | **CLOCK** |
| `is_dig(i)` | `d->params.draw > 0 \|\| d->params.stages_cards` | **DIG** |
| `is_reach(i)` | `d->tmpl == CardTemplate::DirectDamage` (equivalently `params.damage > 0 \|\| landfall_damage > 0 \|\| death_trigger_damage > 0`) | **REACH** |
| `needs_creature(i)` | `p.targeting == Targeting::Creature \|\| p.targeting == Targeting::Multi` | reach live/dead census |
| `is_sac_land(i)` | `p.sacrifice_land` | Volley allowance; land quota `+1` per kept copy |
| `has_landfall(i)` | `p.landfall_damage > 0` | land quota `landfall_bonus` |
| `has_spectacle(i)` | `p.spectacle_cost.has_value()` | dig castability gate |
| `self_taxes(i)` | `p.on_cast_trigger_damage > 0` | sorts last within CLOCK; D1 |
| `has_haste(i)` / `has_prowess(i)` | `d->card.HasKeyword(Keyword::Haste)` / `Keyword::Prowess` | CLOCK order |
| face damage | `ReachFaceForKeeping(i)` (§4), on `FindBurnKillTarget` | REACH order + S0 |

Helpers used: `CleanupDiscardManaValue`, `CleanupDiscardIsLand`, `FindBurnKillTarget`,
`CleanupDiscardRankingWithOrder`. `IsSubtypeCostReducer` is not needed — this deck has no cost
reducer.

---

## 7. State promotions

### Proposed — all are present-tense facts about the board and hand

1. **The dead-conditional promotion (the census).** `needs_creature(i)` with no opponent creature →
   face contribution 0 → head of the shed, ahead of a surplus land. This is the structural analogue
   of Minotaur's KAROO CAVEAT: *a card that cannot function is the first thing shed*. It is the single
   highest-value rule in this proposal, because it covers 8 of the 36 spells and fires in 20% of games
   for the whole game.
2. **The landfall land promotion.** A `landfall_damage` card in hand raises the land quota by one, so
   the drop exists on the Blaze turn. Deliberately consistent with `PreferHoldLandDrop`, which banks
   the drop at the play site for the same reason.
3. **The Shard Volley land allowance.** The k-th `sacrifice_land` burn needs the k-th spare land.
   Cumulative, param-derived, no per-card constant.
4. **The lethal-in-hand promotion.** Once the reach bucket has accumulated
   `s.Opponent().life - board_power`, further burn is overflow and sheds ahead of a surplus land. Pure
   arithmetic on two present-tense numbers, both of which the provider can read.
5. **The dig distance term.** Light Up the Stage is not quota-protected below two sources.

### Rejected as SEARCH-owned — stated so they are not lost

1. **Prowess fuel.** A held noncreature card is +1/+1 on each Swiftspear when cast. Which spell to
   cast, this turn, to cross a damage threshold is a cast decision with a projection behind it —
   exactly the Minotaur Burning-Fist rejection, and the same reasoning.
2. **The prowess self-cast line for Blaze/Blood.** Whether to aim a Blaze at our own Swiftspear is
   enumerated and priced at the cast site (`FindOwnProwessBurnTarget`, `TurnSolver.cpp:13896`). The
   ranking needs one fact from it and has it: the line delivers **zero** face damage.
3. **Shard Volley timing.** `HoldSacLandBurn` already prunes every non-lethal, non-Spectacle Volley
   cast. Re-deriving "don't waste the land" in the discard rule would double-count a decision the
   engine has already made.
4. **A multi-turn clock projection.** `board_power × turns_remaining` is the honest value of a body,
   and I do not use it: it is a projection, and the provider **cannot even read the horizon** —
   `max_turns` lives on `AIEngine::m_max_turns` (`AIEngine.h:278`), not on `GameState`, and the hook
   receives only `const GameState&`. I credit **one swing** (`board_power`) and nothing more.
5. **The Searing Blaze cast-priority tiebreak.** Already built, measured and **rejected**
   (`docs/design/burn-blaze-landfall-audit.md`: 9 of 500 games changed line, 0 changed win turn). Not
   re-proposed.

### On `max_turns = 8` — the brief asked, so: I do NOT use it

The right use would be a hard "dead by the horizon" test — a card needing more land drops than there
are turns left can never be cast, so it sheds below even a conditional card. I decline to implement
it for two reasons. First, the provider cannot read the horizon (above), so it would require
hardcoding `8` in a provider, which is a magic constant describing a *runner default*
(`main.cpp:6782`) that any `--max-turns` invocation changes. Second, on this deck it would be nearly
inert: every card costs 1-3 mana, so "needs more land drops than turns remain" is essentially
unreachable before turn 6, and the deck wins at ~4.3. If the horizon is wanted, the clean route is to
expose it on `GameState` first; I flag that rather than smuggle the constant in.

---

## 8. Doubts, flagged — the user reviews and amends these

**D1 — the big one. Eidolon of the Great Revel is engine-protected from the shed, and in this engine
it is the deck's worst card.** It is a `mulligan.required_pieces` entry and `m_discard_protect`
defaults to `All`, so *every* copy is unsheddable and the provider cannot override it (protected
indices are dropped before tier A). Meanwhile: the goldfish opponent never casts a spell, so
`on_cast_trigger_damage` fires **only on us** — `state.players[active].life -= 2` per spell of MV ≤ 3
(`SpellEffects.h:2897`), and *every* nonland card in this deck is MV ≤ 3. Two Eidolons out is 4 life
per spell. And this is **not** free: `lethal = probe.ActivePlayer().life > 0 && OpponentHasLost(probe)`
(`AIEngine.cpp:2020`) and the rollout's self-kill guards (`TurnSolver.cpp:40006`, `:41464`, `:41643`)
reject a line that kills us on the way. Its learned marginals are the only negative ones among the
creatures (**-0.133** first copy, **-0.102** second). So the mulligan profile's `required_pieces`
protects the deck's worst card from a shed that the rest of this policy would correctly take.
**Recommendation: drop Eidolon from `mulligan.required_pieces`, or set the deck's scope to
`hand`/`deck`.** I can do neither from here — `required_pieces` is a generated artifact and the
protection is engine-enforced upstream of the provider. I have expressed what a provider *can*
express (self-taxing bodies sort last within CLOCK, §4), and it is inert until D1 is resolved.

**D2 — should the census read the FUTURE spawn schedule?** `state.opponent_spawns` is a live pointer
to the game's pattern (`GameState.h:417`), so the provider *could* see that a 4/4 arrives on T3 and
keep a Searing Blood at the T2 cleanup. I propose **present-tense only**, plus an
`MTG_BURN_DISCARD_SPAWNS` lever, on the grounds that the spawn table is test scaffolding and a rule
tuned to it would not transfer to a real opponent. The cost of being wrong is bounded — patterns
4/5/6 spawn on T1 and the deck rarely has 8 cards before T3 — but it is a judgement the cards do not
settle, and I may be over-weighting transferability on a goldfish-only engine.

**D3 — `kLandTarget = 3`.** Taken from the deck's own `PreferHoldLandDrop` threshold and its "curve
tops out at MV 2" comment. But casting two 2-drops in a turn wants 4, and Light Up the Stage at full
cost is `{2}{R}`. Minotaur's analogue ("we do not ever need more than 5") was a user ruling, and this
probably wants one too. If 4 is right, `land_need` rises by one across the board and S1 fires less
often.

**D4 — CLOCK quota 2 vs 3.** Untestable while D1 stands, since no clock card can be shed. Stated for
completeness; if D1 is resolved this becomes a real, measurable question.

**D5 — `card_scores` are 1,281 `src` commits stale, and unlike Minotaur's they are LIVE.**
`burn.profile.json` was last touched by `a2ba8712` (2026-07-14), itself only the one-folder-per-deck
file move; the numbers were generated 2026-07-07. Per the Minotaur round-4 lesson I used them only as
corroborating direction and never as the order. But note the asymmetry with Minotaur: its
`hand_score_threshold` is `-1e+18`, so its scores are nearly inert, whereas **burn's is a real number
(0.4635)** and gates `AIEngine.cpp:1005`. A stale *live* gate is a bigger deal than a stale dead one.
Not this policy's problem to fix, and the correct order if anyone acts on it is Minotaur's:
**regenerate on the current engine first, then measure.**

**D6 — the shed may barely happen in real play, and that does not make the rule inert.** The 8-card
cleanup needs land screw: the deck wins **499/500 at avg 4.33 turns** (`burn-blaze-landfall-audit.md`,
500 games, d5, seed 2002) and dumps its hand, and staged cards do **not** count toward the hand limit
(`TurnSolver.cpp:29711`) so Light Up the Stage never causes a shed. I expect real-play sheds near zero
and rollout sheds in the hundreds of thousands, the Minotaur pattern — **but I did not measure it**
(§0). The adoption bar is non-inferiority plus doctrine quality, so this does not block; it does mean
nobody should expect the metric to move, and that the honest case for shipping is rollout and
mulligan-generation fidelity.

**D7 — two `[bracket note]`s, in opposite directions.** Goblin Guide's attack trigger is
`[Trigger not modelled in Phase 1.2]`, and since that trigger is a *downside* in paper (the defender
may get a free land), **the engine's Goblin Guide is strictly better than the real card** — so its
0.794 marginal is partly an artefact of the omission. Nothing in my order hinges on it (the clock
ranks first anyway, and it is protected), but a future reader should not take that number at face
value. Skullcrack's `[Life-gain lock and damage-prevention lock not modelled]` is genuinely inert
against a passive goldfish that never gains life or prevents damage, so its 3-to-face is faithful.

**D8 — a deckbuilding signal I am flagging rather than acting on.** Searing Blaze and Searing Blood
are 8 of 36 spells, they are the two worst learned cards in the deck, and the engine's own opponent
model presents no creature in 20% of games and a single 1/1 or 2/2 in most of the rest. That suggests
eight slots of creature-conditional burn may simply be wrong *for this goldfish* — which is a
`deck-screening.md` question (one apparatus, arms that swap counts), not a discard question. I raise
it because the discard analysis is what surfaced it, and because it would change this policy's most
valuable rule from "a frequent fire" to "a rare one".

**D9 — one thing no guard can catch, per the brief.** The engine models the two halves of this deck to
different depths. The unconditional burn (Bolt, Skullcrack, Volley) is modelled completely; the
conditional burn is modelled against a *scripted spawn table* rather than an opponent who deploys
creatures in response to pressure. So any comparison that pits one against the other — including this
ranking's own REACH order — is partly measuring the fidelity gap, not the cards. I do not think it
changes the order (a card that is uncastable is uncastable), but it bounds how much confidence the S0
rule's *ordering* of Blaze versus Blood deserves.

---

## Validation route (for integration, not run here)

1. `MTG_SHED_STATS=1` census, 200 games at play settings (§0) — establishes the denominator.
2. `MTG_TRACE=discard` on a short d5 run — eyeball index 0 against §5's worked hand.
3. `test/tools/discard_behaviour_diff.py`, `MTG_BURN_BUCKET_DISCARD` on vs `=0` — the cheap half, and
   on the Minotaur precedent the half that finds the defects. Expect the signature "stops shedding
   Light Up the Stage, starts shedding Searing Blaze/Blood".
4. `MTG_DISCARD_SHED_VERIFY=1` on a smoke run — confirms `CleanupDiscardShedStable() == true`.
5. Smoke, then regression, through the accept flow. Burn's cells: d0 `1001×1000`, d3 `1001×300`,
   d5 `1001×250`; overnight d0 `2002×1000`, d3 `2002×500`; plus **`burn2hg` d3 `1001×50`**, which is
   the 30-life cell that exercises the life-based REACH quota and would catch a hardcoded 20.
6. Watch CI for the Windows/determinism-parity job on push.
