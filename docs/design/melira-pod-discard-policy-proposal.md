# Melira Pod — cleanup-discard BUCKET policy (PROPOSAL, 2026-09-23)

Authored per `docs/design/discard-bucket-authoring-brief.md` for the 2026-09-23 per-deck sweep.
**This document is the deliverable; no `.cpp`/`.h` was touched.** The shipped form would be
`MeliraPodProvider::CleanupDiscardCandidates`, returning a shed order (most expendable first) routed
through `CleanupDiscardRankingWithOrder(s, required_pieces, shed)` so the staged-card and
required-piece protections stay engine-enforced, behind a default-on
`EnvOn("MTG_POD_BUCKET_DISCARD", true)` with `=0` restoring
`GenericProvider::CleanupDiscardCandidates`.

Every card claim below was read out of `src/cards/data/cards.json` for this run (Rule 0); the
`params` keyed on are cited per row. The deck's existing provider hooks
(`MeliraPodProvider::TutorCandidates` / `TutorHandPutList` / `ReviveCandidates` / `PutTargetOk`,
`NotePodRoles`, `PodMissingRoleScore`) were read in full, and the bucket hierarchy below is
deliberately the SAME role order they already implement.

---

## 1. Evidence (measured for this proposal, not asserted)

Two runs of the shipped binary at `HEAD` (`d9c4162a`), both on the deck's own profile.

### The rule is invisible, not inert — and the real-play site is the deck's *worst* games

`MTG_SHED_STATS=1`, the deck's own smoke cells:

| cell | real-play sheds | rollout sheds | of those, `<4` lands | sheds/cleanup |
|---|---|---|---|---|
| **d3 / b10, 50 games, s1001** | **0** | **685** | **685 (100%)** | 1.00 |
| **d0, 1000 games, s1001** | **19** | 0 (no search at d0) | — | — |

At the shipped depth the rule runs **13.7 times per game inside the search and zero times in real
play**, and **100%** of those rollout sheds are taken with fewer than four lands on the battlefield —
the same shape recorded for Minotaur (2,528x) and Dragons (10,020x), and exactly the state where
"shed the most expensive card" is least defensible on a deck whose payoffs sit at MV 3-5. Index 0
decides every one of them with no search above it.

### The 19 real-play sheds, with `MTG_TRACE=discard` (d0, 1000 games, s1001)

Every single one reads `hand=8 cands=8 landsinhand=0 dropopen=1`, with `lip` (lands in play) 1-3.
Three facts fall straight out of that, and all three shape the policy:

* **`cands=8` of `hand=8`, 19 times out of 19.** Nothing was protected. This deck's
  `mulligan.required_pieces` is `[]` (see `decks/Melira Pod/Melira Pod.profile.json`), so
  `CleanupDiscardProtected` returns false for every card and **the engine's required-piece
  protection covers NOTHING here.** See §7.
* **`landsinhand=0`, 19 times out of 19.** The real-play shed is *never* a spare land. The mana
  bucket's surplus rule is a rollout rule on this deck, not a play rule.
* **`lip=1..3` and `dropopen=1`.** The shed always happens mana-screwed with a land drop going
  unused because there is no land to play.

What the current generic max-MV fallback actually pitched:

| shed | count | what it is |
|---|---|---|
| Birthing Pod | 4 | the deck's ENGINE |
| Murderous Redcap | 3 | the persist payload that *kills* |
| Kitchen Finks | 3 | the persist loop body |
| Ranger of Eos | 3 | tutor for the outlet + a dork |
| Reveillark | 2 | recursion |
| Chord of Calling | 2 | the other tutor |
| Ravenous Chupacabra | 1 | **provably zero payoff in this format** (its own `cards.json` note) |
| Celes, Rune Knight | 1 | needs `{R}`; **no land in this deck produces `{R}`** |

**12 of 19 sheds (63%) threw away the engine or a combo piece**, while the two structurally dead
cards sitting in those same hands went once each. `gi=351` (seed 1352) is the worked case: it shed
five turns running — T4 Murderous Redcap, T5 Birthing Pod, T6 Ranger of Eos, T7 Kitchen Finks, T8
Kitchen Finks — i.e. the max-MV rule dismantled the entire kill, in order, and the game was lost.

### Read the upside honestly

Only **11 of 1000** d0 games reach a cleanup shed at all, and **10 of those 11 are unwon** at the
8-turn cap against a **70/1000 (7%)** base rate. The shed is a *symptom* of the screw: a ranking
cannot fix a one-land board, and most of those games were already lost. So the direct metric upside
in real play is small and bounded. The case for the policy is (a) the 685-per-50-games rollout
channel, which biases every line the search scores in the land-light state, and (b) doctrine: a
combo deck must not shed its combo. `real == 0` at d3 does not make the rule inert, it makes it
invisible.

---

## 2. The deck, in one paragraph — therefore its SHAPE

Melira Pod is a **three-part persist combo with a tutor engine**, and it is a combo deck by the
brief's taxonomy, not a midrange one. The kill is: a **counter-prevention enabler**
(Melira / Vizier of Remedies) + a **persist body** (Kitchen Finks / Murderous Redcap) + a **free sac
outlet** (Carrion Feeder / Bloodthrone Vampire) — the enabler eats the persist return's `-1/-1`
counter, so the body returns clean and the outlet can sacrifice it again forever. With Murderous
Redcap the loop *is* the kill (unbounded ETB damage, no attack step needed); with Kitchen Finks it is
unbounded life plus unbounded Carrion Feeder growth, which needs an attack. The deck reaches those
three pieces with **Birthing Pod** (repeatable, `+1` MV climb, eats a creature) and **Chord of
Calling** (one-shot, convoke, fetches any MV ≤ X), backed by 22 lands + 6 mana dorks and a toolbox of
bodies whose real job is to be Pod rungs. So the buckets are **one per combo part, similar effects
grouped** (enabler / outlet / persist, the last sub-split by payload), **plus the tutor bucket** the
brief's combo row asks for, **plus mana split lands/dorks**. Six buckets, and the residual toolbox
still has to be totally ordered.

**The role hierarchy is not mine to choose.** The user ruled it on 2026-09-05 for the tutors —
*counter-prevention enabler > free sac outlet > persist body*, each only while ABSENT from
battlefield+hand — and `PodMissingRoleScore` ships it as 100 / 80 / 60. That is already a
net-of-board bucket hierarchy, so **the discard buckets are the same roles at the same priority**: a
deck cannot rationally tutor for X first and shed X first.

---

## 3. Card-by-card role table (all 28 names / 60 cards)

MV is `ManaCost::ManaValue()` as the engine computes it: hybrid and phyrexian pips are baked into
their first/mana colour, `{X}` counts 0.

| card | n | cost | MV | bucket | `params` keyed on |
|---|---|---|---|---|---|
| Forest | 1 | — | 0 | MANA / lands | `produces [G]` |
| Llanowar Wastes | 4 | — | 0 | MANA / lands (painland) | `produces [B,G,C]`, `tap_self_damage 1` |
| Caves of Koilos | 2 | — | 0 | MANA / lands (painland) | `produces [W,B,C]`, `tap_self_damage 1` |
| Blooming Marsh | 4 | — | 0 | MANA / lands (**fastland**) | `produces [B,G]`, `fastland_max_other_lands 2` |
| Razorverge Thicket | 4 | — | 0 | MANA / lands (**fastland**) | `produces [G,W]`, `fastland_max_other_lands 2` |
| Darkbore Pathway | 2 | — | 0 | MANA / lands (MDFC) | `produces [B]`, `mdfc_back_produces [G]` |
| Branchloft Pathway | 4 | — | 0 | MANA / lands (MDFC) | `produces [G]`, `mdfc_back_produces [W]` |
| Orzhov Basilica | 1 | — | 0 | MANA / lands (**KAROO**) | `enters_tapped`, `etb_bounce_land`, `produces_amount 2` |
| Ignoble Hierarch | 4 | `{G}` | 1 | MANA / **dorks** | `tmpl == ManaDork`, `produces [B,R,G]` |
| Birds of Paradise | 2 | `{G}` | 1 | MANA / **dorks** | `tmpl == ManaDork`, `produces [W,U,B,R,G]` |
| Melira, Sylvok Outcast | 3 | `{1}{G}` | 2 | **ENABLER** | `prevents_minus_counters` |
| Vizier of Remedies | 1 | `{1}{W}` | 2 | **ENABLER** | `reduces_minus_counters_by_one` |
| Carrion Feeder | 4 | `{B}` | 1 | **OUTLET** | `sac_creature_outlet`, no `sac_creature_cost`, `sac_outlet_add_counter_to_self 1` |
| Bloodthrone Vampire | 1 | `{1}{B}` | 2 | **OUTLET** | `sac_creature_outlet`, no `sac_creature_cost`, `sac_outlet_self_pump_power/toughness 2` |
| Kitchen Finks | 4 | `{1}{G/W}{G/W}` | 3 | **PERSIST / body** | `persist`, `etb_self_lifegain 2` |
| Murderous Redcap | 2 | `{2}{B/R}{B/R}` | 4 | **PERSIST / payload** | `persist`, `etb_damage_any 2`, `etb_damage_equals_power` |
| Birthing Pod | 4 | `{3}{G/P}` | 4 | **TUTOR** (engine) | `pod_mv_delta 1`, `pod_activation_cost {1}{G/P}`, `pod_taps` |
| Chord of Calling | 3 | `{X}{G}{G}{G}` | 3 | **TUTOR** (one-shot) | `convoke`, `tutor_to_battlefield_single`, `tutor_mv_max_is_x` |
| Recruiter of the Guard | 1 | `{2}{W}` | 3 | TOOLBOX / piece-proxy | `tutor_to_hand`, `tutor_max_toughness 2` |
| Ranger of Eos | 1 | `{3}{W}` | 4 | TOOLBOX / piece-proxy | `tutor_to_hand`, `etb_tutor_hand_count 2`, `tutor_max_mv 1` |
| Voice of Resurgence | 1 | `{G}{W}` | 2 | TOOLBOX / Pod fuel | `dies_watch_includes_self`, `dies_trigger_creates_tokens 1` |
| Scavenging Ooze | 1 | `{1}{G}` | 2 | TOOLBOX / sink | `gy_exile_grow_cost {G}`, `gy_exile_grow_counters 1` |
| Reveillark | 1 | `{4}{W}` | 5 | TOOLBOX / recursion | `ltb_return_creatures 2`, `ltb_return_max_power 2`, `evoke_cost {5}{W}` |
| Felidar Guardian | 1 | `{3}{W}` | 4 | TOOLBOX / flicker | `etb_blink_permanent` |
| Celes, Rune Knight | 1 | `{1}{R}{W}{B}` | 4 | TOOLBOX / **board-only enabler** | `other_creature_gy_enter_team_counters 1`, `etb_discard_any_number` |
| Reclamation Sage | 1 | `{2}{G}` | 3 | TOOLBOX / inert body | **`{}`** (`vanilla_creature`) |
| Severance Priest | 1 | `{W}{B}{G}` | 3 | TOOLBOX / inert body | **`{}`** (`vanilla_creature`) |
| Ravenous Chupacabra | 1 | `{2}{B}{B}` | 4 | TOOLBOX / inert body | `etb_destroy_opp_creature` (payoff provably 0 here) |

### Genuinely ambiguous roles, flagged

* **Celes, Rune Knight** is a *combo piece by the user's own ruling* ("Celes is primarily a Melira
  replacement", 2026-09-05 — her graveyard-enter `+1/+1` annihilates the persist return's `-1/-1`
  under CR 704.5r) **but only on the battlefield.** `NotePodRoles` counts her `have_prev`
  **battlefield-only** and says why: "her triggers do nothing from hand, and in this deck she is
  essentially uncastable from hand anyway (no red land)". I verified that: **no land in the 22-land
  manabase produces `{R}`** — only Ignoble Hierarch (`B,R,G`) and Birds of Paradise do. So a Celes in
  hand fills no role, and her designed route (a Pod/Chord put from the LIBRARY) is already forfeited
  by her being in hand. She is classed TOOLBOX, and demoted hard while no `{R}` source exists. This
  is the row most likely to be amended (see D7).
* **Reclamation Sage / Severance Priest / Ravenous Chupacabra** are combo-relevant in real Magic and
  **vanilla bodies in this format** — two carry literally empty `params`, the third's only param is
  documented in `cards.json` as having provably zero payoff against the passive opponent. Note this
  is a bracket-note-class fact, exactly what the brief asks to surface: the engine models these three
  *less completely* than it models the combo, which biases against them — but the incompleteness is
  itself the user-approved reading (the optimal line versus this opponent is to decline), so the
  demotion is sound rather than an artefact.
* **Reveillark** straddles TOOLBOX and a fourth combo part: with a free outlet on the battlefield its
  LTB is on-demand and returns *two* printed-power ≤ 2 creature cards **from the graveyard to the
  battlefield**. Of this deck's 18 creatures, 13 qualify — including Melira (2/2), Vizier (2/1),
  Carrion Feeder (1/1) and Murderous Redcap (2/2), i.e. **one of each combo role** — but NOT Kitchen
  Finks (printed power 3), Ranger (3/2), Severance Priest (3/3) or Celes (4/4). That asymmetry drives
  promotion P6, and it is the one place I would not ship without measuring.

---

## 4. The buckets, with quotas (all stated NET OF BOARD)

Census the battlefield first, every time: a role already on the battlefield needs no hand copy. On
this deck that single rule also delivers the "combo is live" collapse for free — once enabler +
outlet + persist body are all on the battlefield, every combo quota is already satisfied and the
whole hand becomes sheddable, which is the right answer and needs no special case.

### B1 — MANA. Quota **4** sources net of board; **+1 while a Chord of Calling is in hand**; 5th otherwise soft.
*Filled from the battlefield by:* any land, plus any permanent with a live mana tap (the dorks).
Birthing Pod is **not** mana.

Why four and not five: the deck's engine turn is *cast Birthing Pod and activate it the same turn*,
and `cards.json` documents that this costs **four sources**, not six — "paying 2 life FREES A SOURCE,
so T3 can cast Pod `{3}`+2 life AND activate `{1}`+2 life off 4 sources, impossible green-only".
Hard-casting the combo is `1` (Feeder) + `2` (enabler) + `3` (Finks) = 6 mana spread over three
turns, so the fourth source is the last one that buys a turn. The fifth matters only for Reveillark
(MV 5) and for a large Chord X — hence the Chord bump, since Chord's `tutor_mv_max_is_x` makes it the
deck's one genuine mana sink (X = 5 fetches Reveillark and wants 8 mana).

**Colour coverage comes first** and binds the quota: the minimal set of board+hand sources that
covers **G, W, B**. `{R}` is deliberately excluded from coverage — only Celes needs it and only a
dork can make it, so R is a dork question, not a land question.

*Sub-split (preference only; the parent total binds, per the brief's rule 3):*
* **DORKS outrank surplus lands.** Ignoble Hierarch and Birds of Paradise are mana *and* combo
  infrastructure: a MV-1 creature is Birthing Pod rung 1 (pods into Vizier / Voice / Scooze /
  Bloodthrone at MV 2), a convoke body for Chord (`ClassifyConvokeBodies` excludes dorks with a live
  mana tap by dominance but *includes* summoning-sick ones), free Carrion Feeder fodder, and the only
  `{R}` source in the deck. A dork is never "surplus mana"; past the mana quota it rejoins the
  toolbox above the inert bodies, not below the lands.
* **Lands**, worst-first among the surplus (see §5).

### B2 — ENABLER (counter-prevention). Quota **1** net of board.
*Group:* Melira ×3 (`prevents_minus_counters`) + Vizier of Remedies ×1
(`reduces_minus_counters_by_one`). **The grouping is verified from card data, not assumed:** Vizier
is modelled faithfully as `n -> max(0, n-1)` and its own note states "For persist (n=1) both yield 0,
which is why either card enables the loop"; they diverge only at n ≥ 2, which nothing in this deck
produces. Quota 1 because a second enabler changes nothing (and Melira is Legendary, so a second copy
on the battlefield dies to the legend rule).
*Also filled from the battlefield by* Celes (`other_creature_gy_enter_team_counters > 0`) —
**battlefield only**, mirroring `NotePodRoles` exactly, so a hand Celes never suppresses protecting a
real enabler.
*Within-bucket keep order:* Vizier ≥ Melira is a coin-flip on card data (same MV, `{1}{W}` vs
`{1}{G}`) — resolved by the distance term (whichever colour board+hand actually covers), then by
Melira's legend clash if one is already out. Surplus copies drop to the toolbox as MV-2 bodies /
Pod rung-2 fuel.

### B3 — FREE SAC OUTLET. Quota **1** net of board.
*Group:* Carrion Feeder ×4 + Bloodthrone Vampire ×1. Both verified free from `cards.json`:
`sac_creature_outlet` with **no `sac_creature_cost`** and no `{T}` — the exact predicate
`NotePodRoles` / `PodMissingRoleScore` / `TutorHandPutList` already use.
*Within-bucket keep order:* **Carrion Feeder first** — `{B}` vs `{1}{B}`, and its payload is a
PERMANENT `+1/+1` counter (`sac_outlet_add_counter_to_self`), which its own note calls "under the
Kitchen Finks persist loop the deck's combat wincon", against Bloodthrone's until-end-of-turn
`+2/+2`. So the surplus shed order is **Bloodthrone Vampire before a spare Carrion Feeder**.
(`Melira Pod.buckets.json` keeps the two *split* for the keep table — "merging the two sac outlets is
not entirely ideal" — which is a mulligan-feature ruling, not a claim that the role needs two cards.
I read it as support for this ordering, with the role quota staying 1.)

### B4 — PERSIST BODY. Parent quota **2** net of board, sub-split by PAYLOAD.
* **PAYLOAD** — Murderous Redcap (`persist && etb_damage_any > 0`): quota **1**. This is the loop
  that *wins on its own* — unbounded ETB damage, no attack step, no summoning sickness.
* **BODY** — Kitchen Finks (`persist && etb_self_lifegain > 0`): quota **1**. The cheap loop body
  (MV 3 vs 4, 4 copies vs 2), Pod rung 3 (pods straight into Redcap at MV 4), and the lifegain.
* Fungible upward: a hand with no Redcap keeps a second Finks.

**The sub-split is the engine's own, not an invention.** `NotePodRoles` tracks `have_lifegain` and
`have_damage` as *separate* fields, `PutTargetPolicy` puts both in `any_missing`, and
`ReviveCandidates` scores `+60` only for `persist && etb_damage_any > 0`. The deck's fetch policy
already treats the two persist payloads as distinct roles; the discard policy agrees with it.

### B5 — TUTOR / ENGINE. Quota **1** net of board.
*Group:* Birthing Pod ×4 (`pod_mv_delta != 0`) + Chord of Calling ×3
(`tutor_to_battlefield_single` + `convoke`). *Filled from the battlefield by* a resolved Birthing Pod
(`pod_mv_delta != 0` on a permanent — the same test `NotePodRoles` uses for `pod_active`); nothing on
the battlefield can fill Chord's slot, since Chord is a spell.
*Within-bucket keep order:* **Chord, then Pod** — Chord fetches the exact missing piece at any rung
with no creature sacrificed and convoke pays with bodies the deck already has, whereas Pod is 4 mana
plus `{1}{G/P}` per activation and must eat a creature to climb exactly one rung. Weakly corroborated
by the deck's learned first-copy marginals (Chord **+0.332**, Birthing Pod **−0.273** — the largest
gap between two cards in one role), but those are stale and confounded, so this is flagged as **D1**
with a named A/B lever rather than claimed.

### B6 — TOOLBOX (the residual; still totally ordered).
Keep order, best-kept first (so the shed order is its reverse):

1. **`tutor_to_hand` while a combo role is MISSING from board+hand** — Recruiter of the Guard, then
   Ranger of Eos. **Recruiter is a one-card proxy for *any* missing piece**, and I checked this rather
   than assuming: `tutor_max_toughness 2` reaches Melira (2/2), Vizier (2/1), Carrion Feeder (1/1),
   Bloodthrone (1/1), Murderous Redcap (2/2) **and** Kitchen Finks (3/**2**) — every combo role.
   Ranger's `tutor_max_mv 1` reaches only Carrion Feeder + the dorks (`cards.json` states that legal
   pool explicitly), so it covers the outlet role and mana, not the enabler. When every role is
   filled, both demote to plain bodies (Recruiter 1/1 MV 3, Ranger 3/2 MV 4).
2. **Voice of Resurgence** (MV 2) — `dies_trigger_creates_tokens 1` with
   `dies_watch_includes_self`: it replaces itself on death, so it is *two* Pod sacrifices / two
   Carrion Feeder activations. It is the only body `MeliraPodProvider::PutTargetOk` admits purely as
   Pod fuel.
3. **Scavenging Ooze** (MV 2) — repeatable `{G}` growth is a real clock and a mana sink; demoted
   slightly because exiling our own creature cards strips Reveillark's LTB targets (its own note).
4. **Reveillark** (MV 5) — the recursion engine (and see P6).
5. **Felidar Guardian** (MV 4) — flicker resets a spent persist counter and untaps a tapped Pod for a
   second activation, but it is deliberately **not** on the user's Pod/Chord whitelist
   ("Felidar is not on the user's whitelist and is not offered") and is the deck's worst learned card.
6. **Reclamation Sage** (MV 3, `{2}{G}` — one coloured pip, a 2/1 body).
7. **Severance Priest** (MV 3, `{W}{B}{G}` — three coloured pips, a 3/3 body; ordered against Rec Sage
   by the distance term rather than a fixed rank, see D9).
8. **Ravenous Chupacabra** (MV 4, `{2}{B}{B}` — the deck's hardest colour requirement on a body whose
   ETB is provably dead here).
9. **Celes, Rune Knight** while **no `{R}` source is in board+hand** — structurally uncastable, see §3.

`Melira Pod.buckets.json` records two USER merges that this stratum must stay consistent with:
{Voice of Resurgence, Scavenging Ooze} as "two-drop pod pieces that are pretty much just podded into
something else" and {Reclamation Sage, Severance Priest} as the three-drop equivalent. Both merges are
kept *adjacent* here, split only on the param facts above.

---

## 5. Within-bucket order and the distance-to-playable term

### The two structurally DEAD classes come before any ranking
1. **A dead KAROO.** Orzhov Basilica (`etb_bounce_land`) counts as a land only if there is another
   land to bounce — board has another land, or the hand holds another non-Karoo land. With none it
   must return **itself** and is a blank, not a land. This is the recorded cross-deck caveat
   (Minotaur's Rakdos Carnarium, Dragons' Gruul Turf; found for real on Minotaur seed 1001 gi=27,
   which played zero lands in eight turns), and the engine's `BounceKarooLand` "prefers a tapped
   land", which does not save the one-land case. One copy here, so the effect is small — but it is
   free to get right and it is index 0 whenever it fires.
2. **A colour-IMPOSSIBLE card.** A card requiring a coloured pip that no source in board+hand can
   produce. On this decklist that is Celes, Rune Knight and only Celes (`{R}`; 22 lands, none red).

### Surplus lands, worst-first
* **Fastlands enter TAPPED once `board_lands >= 3`** (`fastland_max_other_lands 2`: "enters tapped
  unless you control two or fewer other lands"). That is 8 of the 22 lands (Blooming Marsh,
  Razorverge Thicket), and it is a genuine, param-derivable, state-dependent demotion in the same
  family as the Karoo caveat: past the third land, a fastland in hand is strictly the worst source
  for *this* turn's mana. Shed it before a painland or a Pathway.
* Then the land that adds **no new colour** to board+hand coverage.
* **Painlands are NOT demoted for their life cost.** `tap_self_damage 1` is nearly free in goldfish —
  the opponent never attacks — so Llanowar Wastes and Caves of Koilos are effectively untapped duals
  (triple, counting `{C}`) here. Life matters only as phyrexian/Pod fuel, and Kitchen Finks gains it
  back. Stating this because "painland = worse land" is the intuition and it is wrong in this format.
* **Dorks are never surplus mana** (see B1).

### The distance-to-playable term (bounded and reach-conditional, `MTG_POD_DISCARD_DIST`)
Reach = live mana sources on the battlefield + live lands in hand (a dead Karoo is not live).
Inside an over-full bucket, a card of effective MV ≥ reach + 2 ranks below a cheaper card of the same
role. Kept deliberately narrow, because the Minotaur round-1 result is on record: a
lexicographic "playability first" sort measured **worse**, monotonically in how hard playability was
weighted. The three places this deck actually needs it:
* Reveillark at MV 5 (or `evoke_cost {5}{W}` at 6) versus the MV-2/3 toolbox.
* Severance Priest `{W}{B}{G}` versus Reclamation Sage `{2}{G}` (three pips versus one).
* Ravenous Chupacabra `{2}{B}{B}` — `BB` is the deck's hardest requirement.

**Two traps the term must dodge, both verified in the source:**
* **Birthing Pod's `{G/P}` is baked into the flat cost as a GREEN pip** (`ManaCost`: "the pip's
  COLOUR is baked into the flat ints ... so ManaValue, colour demand, equality and every flat reader
  are byte-identical"). A naive colour-exactness test therefore says Pod needs `{G}` when **2 life
  pays it**, and would demote the deck's engine in exactly the hands where it is castable. Any
  distance term must check `phyrexian_count > 0` and discount the pip.
* **MDFC lands read as their FRONT colour only in hand**, a disclosed simplification
  (`cards.json`: "In hand it counts as its FRONT colour for mulligan/colour eval"). So a hand
  Branchloft Pathway reads G-only (its `{W}` face invisible) and Darkbore reads B-only (its `{G}`
  face invisible) — under-counting W and G respectively across 6 of the 22 lands. For coverage I
  propose unioning `produces` with `mdfc_back_produces` (a Pathway *can* supply either) while never
  counting both at once, and explicitly **not** re-implementing colour-exact affordability / Hall's
  condition inside a cleanup ranking. See D4.

---

## 6. The total order over a hand (index 0 is always determined)

The quotas over-subscribe a seven-card hand by construction (`4 mana + 1 + 1 + 2 + 1 = 9`), so — as
Minotaur's shipped implementation found the hard way — **the ladder is what actually decides, and it
must be interleaved rather than filled bucket-at-a-time.**

**KEEP ladder (protected slots, in fill order; read backwards it is the order protected cards give
way):**

```
mana1 > enabler1 > mana2 > outlet1 > mana3 > persist1 > tutor1 > mana4 > persist2 > mana5
```

* `mana1` first: a hand with zero sources does nothing. (Minotaur ships the same `land1 > threat1`
  opening.)
* `enabler1 > outlet1 > persist1` is the USER-ruled role order, unchanged.
* `persist1` is Redcap when no damage payload exists in board+hand, else Finks.
* `tutor1` **promotes above `persist1` when no persist body is in board+hand at all** — a tutor
  stands in for the piece it would fetch, and with no body anywhere the tutor is the only route to
  one. (`MTG_POD_DISCARD_TUTORFIRST` makes the promotion unconditional; see D1.)
* `mana5` is protected only while colour coverage over G/W/B is incomplete, or a Chord is in hand.

**SHED order returned to the engine (index 0 first):**

1. A **dead Karoo**.
2. A **colour-impossible** card (Celes with no `{R}` source).
3. **Surplus mana** beyond the quota — fastlands-that-enter-tapped first, then no-new-colour, then
   the rest; dorks excluded.
4. The **TOOLBOX**, reverse of the §4/B6 keep order: Chupacabra → Severance Priest → Reclamation Sage
   → Felidar → Reveillark → Scavenging Ooze → Voice → Ranger → Recruiter, reordered inside by the
   distance term, with the two `tutor_to_hand` cards promoted out of shed range while any combo role
   is missing.
5. **Surplus combo copies** past their bucket quota, re-entered at their body/fuel value: a spare
   Carrion Feeder (MV 1, the cheapest Pod fuel there is) near the top of the keeps; a spare
   Melira/Vizier as an MV-2 body beside Voice; a spare Finks/Redcap as the persist parent's overflow;
   Bloodthrone before a spare Feeder.
6. Only if everything above is exhausted, the **ladder in reverse** — `mana5`, `persist2`, `mana4`,
   `tutor1`, `persist1`, `mana3`, `outlet1`, `mana2`, `enabler1`, `mana1`.

**The list names every card in the hand.** That is not optional: anything omitted falls through to the
shared tier B, which is descending mana value — the very ranking §1 measures as backwards on this deck
(Pod ×4, Redcap ×3, Finks ×3). Omission = keep is a real mechanism, but a partial list hands the tail
of the decision back to max-MV (the Mirrorwing gi295 lesson).

---

## 7. What `required_pieces` already covers: **NOTHING on this deck**

`decks/Melira Pod/Melira Pod.profile.json` has `"mulligan": { ... "required_pieces": [] }`.
`CleanupDiscardProtected` returns false immediately when a card's name is not in that list, so **no
card of this deck is engine-protected at the cleanup shed** — confirmed empirically by `cands=8` of
`hand=8` in all 19 real-play sheds (§1). The ranking therefore cannot lean on the protection for
anything, and the quotas above are the *only* thing keeping a combo piece in hand.

**And a warning for whoever populates that list later**, because it would fight this policy rather
than help it: `discard_protect` defaults to `DiscardProtectScope::All`, which protects **every** copy
of a listed name — so listing "Kitchen Finks" would protect all four and make the "surplus copies are
sheddable" rule unreachable, pushing the shed onto the mana or the toolbox instead. A deck that wants
both would need `discard_protect: "hand"` **plus** `MeliraPodProvider::InterchangeableRequiredGroup`
returning `{Melira, Vizier of Remedies}` and `{Carrion Feeder, Bloodthrone Vampire}` — exactly the
Anti-Lifegain case the hook was built for (s3003 gi226, where name-only counting protected both
enablers and made the deck pitch a payoff). My ranking is written to need neither, and to stay correct
if both appear.

---

## 8. Param-level predicates (so integration is mechanical)

All classification by `params`, never by card name. Every read goes through
`CardDatabase::Instance().LookupCached(card)` — a hand card is a name-only placeholder
(`DeckLoader::MakePlaceholder`), so its own type/cost masks are empty.

| predicate | test | bucket |
|---|---|---|
| `IsMana` / lands | `CleanupDiscardIsLand(card)` | MANA / lands |
| `IsDork` | `d.tmpl == CardTemplate::ManaDork` (as `TutorHandPutList` already does) | MANA / dorks |
| `IsKaroo` | `d.params.etb_bounce_land` | dead-Karoo test |
| `IsFastland` | `d.params.fastland_max_other_lands >= 0` (unset is `-1`) | tapped-late demotion |
| `IsPainland` | `d.params.tap_self_damage > 0` | *no* demotion (documented) |
| `IsMdfc` | `!d.params.mdfc_back_name.empty()` | colour-coverage union |
| `IsEnabler` (hand) | `d.params.prevents_minus_counters \|\| d.params.reduces_minus_counters_by_one` | ENABLER |
| `FillsEnabler` (board) | the above **OR** `d.params.other_creature_gy_enter_team_counters > 0` | ENABLER census |
| `IsFreeOutlet` | `d.params.sac_creature_outlet && !d.params.sac_creature_cost.has_value()` | OUTLET |
| `IsPersistPayload` | `d.params.persist && d.params.etb_damage_any > 0` | PERSIST / payload |
| `IsPersistBody` | `d.params.persist && d.params.etb_self_lifegain > 0` | PERSIST / body |
| `IsPutTutor` | `d.params.pod_mv_delta != 0 \|\| d.params.tutor_to_battlefield_single` | TUTOR |
| `IsPodActive` (board) | `d.params.pod_mv_delta != 0` on a controlled permanent | TUTOR census |
| `IsHandTutor` | `d.params.tutor_to_hand` | TOOLBOX / piece-proxy |
| `IsPodFuel` | `d.params.dies_trigger_creates_tokens > 0` | TOOLBOX / fuel |
| `IsRecursion` | `d.params.ltb_return_creatures > 0` (+ `ltb_return_max_power` for P6) | TOOLBOX |
| `IsManaSink` | `!d.params.gy_exile_grow_cost.empty()` | TOOLBOX |
| `IsFlicker` | `d.params.etb_blink_permanent` | TOOLBOX |
| `IsGoldfishInertBody` | `params` empty (`vanilla_creature`) **or** the only live param is `etb_destroy_opp_creature` / `etb_tap_opp_creature` | TOOLBOX / bottom |
| `ChordInHand` | `d.params.tutor_mv_max_is_x` | mana-quota `+1` |
| combo-active (board) | `MinusCounterReplacement(s, me, 1) == 0 \|\| GyEnterCleanerActive(s, me, -1)` — the exact test `FodderSacUseful` uses | quota collapse |

The "goldfish-inert body" row is the one predicate that needs a named shared helper rather than an
inline check (the `MTG_SKIP_INERT_LIFEGAIN` precedent): "an ETB whose only target class is an
opponent permanent" is inert by construction against the passive opponent, and three decks now have
cards in that class.

Reuse rather than re-derive: `NotePodRoles(s, controller)` in `DecisionProviders.cpp` already computes
`have_prev / have_outlet / have_persist / have_lifegain / have_damage / pod_active` over
battlefield+hand with precisely the semantics this policy needs. The discard ranking wants the
**battlefield-only** variant for its census and the battlefield+hand variant for the
`any_missing` promotion, so it should take a `bool hand_too` parameter rather than being copied.

---

## 9. State promotions

### Proposed (all pure board census — no damage projection, no cast choice)
* **P1 — dead Karoo is index 0.** §5.
* **P2 — fastland demotion once `board_lands >= 3`.** §5. Param-exact, 8 of 22 lands.
* **P3 — colour-impossible demotion.** Celes with no `{R}` producer in board+hand; she promotes back
  above the inert bodies the moment a Hierarch or Birds is available.
* **P4 — hand-tutor promotion while a role is missing.** `tutor_to_hand` cards leave shed range while
  `any_missing` — the identical test `PutTargetPolicy` already computes. Recruiter reaches every role,
  Ranger only the outlet, so Recruiter promotes higher.
* **P5 — Chord raises the mana quota by 1.** `tutor_mv_max_is_x` is a real sink; without this, an
  extra land is graded as dead while the hand holds the card that spends it.
* **P6 — the Reveillark recovery term (`MTG_POD_DISCARD_LARK`, propose default OFF).** While a
  `ltb_return_creatures` permanent **and** a free outlet are both on the battlefield, the LTB is
  on-demand and a discarded creature card of printed power ≤ `ltb_return_max_power` is not lost — it
  is recoverable **onto the battlefield**, for free. So in that state Melira / Vizier / Carrion Feeder
  / Murderous Redcap become *more* expendable and Kitchen Finks (printed power 3) does not. This is
  the Treasure Hunt retrace rule's logic ("a RETRACE required piece is NOT lost to a discard") applied
  to a different mechanism, and it is fully param-derived. **Scoped to reorder only cards already past
  their bucket's quota**, and proposed OFF because it is clever enough to be wrong (D8).

Note what needs no promotion: **the combo-active collapse is already delivered by net-of-board
accounting.** With enabler + outlet + persist body all on the battlefield every combo quota reads
satisfied, so the whole hand becomes sheddable and index 0 is simply the lowest-value card — which is
the right answer. This is the analogue of Minotaur's Neheb inversion needing no special case.

### Deliberately rejected as SEARCH-owned
* **Celes's rummage as "a dead card is draw-fuel."** The `etb_discard_any_number` trigger turns
  surplus cards into draws, so an uncastable card in hand is not pure waste — but that is a CAST
  decision with a card-advantage projection behind it. (It also has its own resolution rule; see D10.)
* **"Shed the piece we can Chord/Pod for this turn."** Needs a mana and fetch projection.
* **Lethal projection** ("shed anything, the loop kills this turn"). A damage projection;
  `FodderSacUseful` already owns the lethal test at its own site, conservatively and on purpose.
* **Pod-chain planning** — which MV rung to keep open for a future climb. A multi-step cast plan.
* **Bottoming / mulligan interaction.** The keep table's business
  (`bottom_eval_depth 0 / topk 5` is already tuned per-deck here).

---

## 10. Doubts, flagged for the user

* **D1 — where `tutor1` sits, and Chord-vs-Pod inside the bucket.** I placed `tutor1` below
  `persist1` (pieces beat the card that fetches pieces) with one conditional promotion, and Chord
  above Pod. The learned first-copy marginals agree loudly (Chord **+0.332**, Pod **−0.273**) but
  they are an **unadjusted opening-hand group-mean difference**, confounded with castability, and
  **stale: 290 `src` commits have landed since this profile was last written.** The Minotaur round-4
  lesson applies verbatim — do not resolve this axis on those numbers. A/B levers:
  `MTG_POD_DISCARD_TUTORFIRST`, `MTG_POD_DISCARD_PODFIRST`.
* **D2 — PERSIST parent quota 2 or 1?** I argued 2 on the grounds that Redcap and Finks are not
  interchangeable (one kills, one does not) and a spare body is Pod fuel. But `persist2` sits at
  ladder slot 9, so it will rarely bind in a seven-card hand — which also means the choice is cheap
  to get wrong, and cheap to test.
* **D3 — MANA quota 4 (+1 with a Chord) versus the keep table's `max_lands: 5`.** The four-source
  figure comes from Pod's phyrexian cast+activate line, which is the most load-bearing arithmetic in
  §4. Note the real-play evidence says the mana quota barely matters at that site at all
  (`landsinhand=0` in 19 of 19), so this is mostly a rollout parameter.
* **D4 — the MDFC hand-side colour simplification.** 6 of 22 lands read as one face in hand. My
  union-for-coverage treatment is the least-wrong cheap option, but it *over*-counts a hand of nothing
  but Pathways. A cleanup ranking is the wrong place to re-derive Hall's condition; if the user wants
  exactness here it is an engine change, not a discard rule.
* **D5 — Birthing Pod's phyrexian pip reads as a green requirement** in every flat cost reader. Any
  distance/colour term must special-case `phyrexian_count > 0` or it will demote the engine.
* **D6 — is {Melira, Vizier} really one bucket?** Card data settles it for persist (n=1 → 0 either
  way) and the user's own tutor hierarchy treats "counter-prevention enabler" as one role. But
  `buckets.json` lists the pair under **`keep_apart`** as "an arguable merge, held back as a
  fallback". I read that as a keep-table feature-space ruling, not a role ruling — flagging it because
  it is the one place my grouping could be read as contradicting a user line.
* **D7 — Celes as the first non-mana shed.** She is a user-designated combo piece (the second Melira
  effect) and I am proposing to pitch her first whenever no `{R}` source exists. The card data
  supports it (no red land; her route is a library put she has already left), but if the user would
  rather never shed a Melira effect, move her above the three inert bodies unconditionally and the
  rest of the order is unaffected.
* **D8 — the Reveillark recovery term (P6).** Real interaction, real risk: it makes the deck *more*
  willing to pitch Melira and Redcap, which is the opposite of the doctrine everywhere else, and it is
  correct only while the Lark and an outlet are both out. Proposed default OFF behind its own lever so
  it is measured alone.
* **D9 — Severance Priest versus Reclamation Sage.** Priest is the bigger body (3/3 vs 2/1) and the
  harder cast (`{W}{B}{G}` vs `{2}{G}`); I let the distance term decide. If `MTG_POD_DISCARD_DIST` is
  not adopted, a fixed order is needed and I would shed Priest first.
* **D10 — this deck would have TWO discard doctrines, and the other one is hard-coded.** Celes's
  `etb_discard_any_number` rummage resolves in `SpellEffects.h` with its own rule — "discard the
  hand's excess lands beyond two" — and does **not** consult `CleanupDiscardCandidates`. On the
  evidence in §1 that rule is close to inert in practice (`landsinhand=0` in every real shed observed,
  so there are usually no excess lands to rummage away), but it means a bucket policy shipped here
  would not govern the deck's *only other* discard site. Routing the rummage through this ranking is a
  one-line change in scope for the integrator, not for this brief — surfacing it rather than assuming.
* **D11 — validation cannot use the rule-vs-searched labeller at shipped depth.** The labeller probes
  the CR 514.1 cleanup, and this deck reaches it **zero times in 50 games at d3**. So the axis has to
  be validated the Minotaur round-3 way: a **d0 behavioural diff**
  (`test/tools/discard_behaviour_diff.py`, seconds, paired within a game and stopped at each game's
  first divergence) to prove the arm fires and to inspect *which* decisions flip, then a **paired
  outcome A/B at d0 and d3** (`test/tools/paired_arms.py`) on fresh seeds disjoint from 1001/2002/3003,
  then smoke + regression through the accept flow. Given 19 real sheds per 1000 d0 games, the d0 cell
  needs a large game count to have any power at all, and the honest expectation is that the **metric
  will not move much** — the case for shipping is the 685-per-50-games rollout channel and doctrine
  correctness, exactly as for Minotaur.

---

## 11. Honest assessment

This deck **barely sheds in real play** — 19 sheds in 1000 d0 games, 0 in 50 d3 games — and 10 of the
11 games that shed at all were already lost. Anyone reading only that would conclude the policy is not
worth shipping, which is the mistake the Minotaur document had to retract: the search sheds **685
times per 50 games** at the shipped depth, **100%** of them at fewer than four lands, at 1.00 cards
per cleanup, and index 0 decides every one with no search above it.

What makes the case here stronger than "doctrine hygiene" is that the incumbent rule is measurably,
specifically backwards on this deck: descending mana value pitched **Birthing Pod four times,
Murderous Redcap three times, Kitchen Finks three times and Chord of Calling twice** — 63% of all
real sheds landing on the engine or a combo piece — while the two cards in those same hands that the
card data marks as having *provably zero payoff in this format* were pitched once each. A combo deck
that sheds its combo and keeps its blanks is not making a close call badly; it is ranking the wrong
axis. Adoption bar: **non-inferiority** plus doctrine quality, validated per D11.
