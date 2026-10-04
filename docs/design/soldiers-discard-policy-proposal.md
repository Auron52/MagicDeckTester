# Soldiers -- cleanup-discard BUCKET policy (proposal, 2026-10-04)

Status: **AUTHORED + IMPLEMENTED, PROVISIONAL -- awaiting USER review.** Shipped default ON per the
authoring brief's implementation contract (`EnvOn("MTG_SOLDIERS_BUCKET_DISCARD", true)`; `=0`
restores `GenericProvider::CleanupDiscardCandidates`, the max-MV fallback). Code:
`SoldiersProvider::CleanupDiscardCandidates` in `src/ai/DecisionProviders.cpp`. Brief:
`docs/design/discard-bucket-authoring-brief.md` (main checkout).

## 1. The deck, in one paragraph

`decks/Soldiers/soldiers.cod`: W/x Human Soldier tribal aggro on Aether Vial. 36 creatures (every
one a Human Soldier), 4 Aether Vial, 20 lands. The curve is 1-3 drops with Rick at 4; the mana
sinks above that are Recruitment Officer's `{3}{W}` dig, King Darien's `{3}{G}{W}` token and
Fortified Beachhead's `{5}`+`{T}` team pump. It needs land drops to about four, bodies, and its lords.
That is the **simple** shape: two buckets (MANA, THREATS) plus a one-copy accelerant (Vial).

**It barely sheds.** `analyze_deck.py --discard-analysis` evidence (400 games, d3): `DISCARD_INERT` --
no cleanup shed reached by EITHER caller (real or rollout). An aggro deck that casts 1-3 cards a
turn and draws one rarely holds eight at cleanup. The policy exists because the brief mandates one
and because rollouts at other depths/budgets may still reach the site; its measured effect at the
suite settings is recorded in the ledger (5i).

## 2. Card-by-card role table (from `cards.json` params)

| card | cost | bucket | keyed on |
|---|---|---|---|
| Plains | land | MANA (basic, W) | `produces [W]` (1 colour) |
| Fortified Beachhead | land | MANA (W/U) | `produces [W,U]` |
| Silent Clearing | land | MANA (W/B) | `produces [W,B]` |
| Unclaimed Territory, Cavern of Souls | land | MANA (any colour, creature spells) | `produces` x6, `colored_creature_only` |
| Secluded Courtyard | land | MANA (best) | + `colored_creature_ability_ok` (also pays Darien's ability) |
| Aether Vial | {1} | VIAL | `upkeep_adds_charge` |
| Champion of the Parish | {W} 1/1 | THREAT (value 4) | P+T 2, `own_creature_enters_self_counters` +2 |
| Esper Sentinel | {W} 1/1 | THREAT (2) | vanilla here (opponent-cast trigger inert) |
| Recruitment Officer | {W} 2/1 | THREAT (4) | P+T 3, `activated_dig_count` +1 |
| Thalia, Guardian of Thraben | {1}{W} 2/1 | THREAT (3) | P+T only (legendary) |
| Thalia's Lieutenant | {1}{W} 1/1 | THREAT (7) | `etb_each_other_own_creature_counters` x3 + self counters x2 |
| Coppercoat Vanguard | {1}{W} 2/2 | THREAT (6) | lord +1/+0 x2 |
| Cathar Commando | {1}{W} 3/1 | THREAT (4) | P+T |
| Jirina, Dauntless General | {W}{B} 2/2 | THREAT (4) | P+T (legendary; B pip) |
| Harbin, Vanguard Aviator | {W}{U} 3/2 | THREAT (7) | P+T + `attack_with_n_team_pump_power` x2 (legendary; U pip) |
| Field Marshal | {1}{W}{W} 2/2 | THREAT (8) | lord +1/+1 x2 |
| General Kudro of Drannith | {1}{W}{B} 3/3 | THREAT (10) | lord +1/+1 x2 (legendary; B pip) |
| Brutal Cathar | {2}{W} 2/2 | THREAT (4) | P+T |
| King Darien XLVIII | {1}{G}{W} 2/3 | THREAT (9) | lord (all) +1/+1 x2 (legendary; G pip) |
| Ranger-Captain of Eos | {1}{W}{W} 3/3 | THREAT (8) | P+T + `tutor_to_hand` +2 |
| Recruiter of the Guard | {2}{W} 1/1 | THREAT (4) | P+T + `tutor_to_hand` +2 |
| Rick, Steadfast Leader | {2}{W}{W} 3/4 | THREAT (15) | P+T + lord +2/+2 x2 (legendary) |

Ambiguous: Champion of the Parish is the deck's best turn-one play and a poor late topdeck; the
value term does not see the turn. Flagged in section 8.

## 3. Buckets and quotas (all net of board)

* **MANA -- lands, quota 4 minus lands on our battlefield.** The curve tops at 4 (Rick, Officer's
  dig); a fifth land only feeds Darien's token / Beachhead's pump. Within the bucket the most
  flexible land is kept (score = number of colours produced, +1 for `colored_creature_ability_ok`):
  Courtyard > Territory = Cavern > Beachhead = Clearing > Plains.
* **THREATS -- the catch-all, no cap,** with a FLOOR of 2 kept ahead of the Vial.
* **VIAL -- 1 copy, early only.** None on board and turn <= 3: kept after the 2-threat floor.
  Turn >= 4: sheds right after the excess lands (a Vial cast late deploys its first creature only a
  turn after the creature could have been cast anyway).

## 4. Within-bucket order and distance-to-playable

Threats shed: (a) NOT castable next turn first -- mana value above `board lands + (1 if a land is in
hand)`, or an off-colour pip (U/B/G/R) that no land on board or in hand produces; then (b) lower
param value first; then (c) higher mana value first.

## 5. Total order over a hand (index 0 first)

1. DEAD: a legendary whose namesake is already on our battlefield; any Vial beyond the first
   (board copy counts).
2. Lands beyond the land quota, worst land first.
3. (turn >= 4) the Vial.
4. Threats beyond the 2-threat floor, in the section-4 order.
5. (turn <= 3) the Vial.
6. The 2 floor threats, lower first.
7. Quota lands, worst first.

Every non-staged hand card lands in exactly one slot, so index 0 is always the policy's choice.

## 6. Predicates (as implemented)

`CleanupDiscardIsLand` -> MANA; `params.upkeep_adds_charge` -> VIAL; `card.HasSupertype(Legendary)`
+ board namesake -> DEAD; everything else -> THREAT. Value = `m_power + m_toughness`
+ `2*(power_bonus+tough_bonus)` if `subtypes_affected` non-empty or `affects_all_creatures`
+ `3*etb_each_other_own_creature_counters` + `2*own_creature_enters_self_counters`
+ `2*attack_with_n_team_pump_power` + 2 if `tutor_to_hand` + 1 if `activated_dig_count`.
Land score = `params.produces.size()` + 1 if `colored_creature_ability_ok`.

## 7. State promotions

Implemented: none beyond the board-netted quotas and the turn gate on the Vial.
Rejected as search-owned: "keep the fourth Human for Rick's anthem" and "keep a fifth Soldier for
Harbin / Beachhead" -- both are cast-and-attack projections, not cleanup rankings.

## 8. Doubts (for the user)

1. **Vial turn gate (<= 3).** A guess; the deck rarely sheds at all, so it is unmeasurable here.
2. **Champion late.** A turn-agnostic value keeps a late Champion over e.g. Esper Sentinel; arguably
   right (it still grows) but unverified.
3. **Land quota 4.** Officer's dig and Beachhead's pump would like a fifth; we chose 4 because the
   deck's cards are all castable on 4 and the shed only bites with a full grip.
4. **Rick's anthem threshold.** Rick is valued as a full +2/+2 lord even with fewer than 4 Humans.
