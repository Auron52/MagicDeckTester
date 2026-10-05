# Bruna — cleanup-discard BUCKET policy (PROPOSAL, 2026-10-05)

Authored from `docs/design/discard-bucket-authoring-brief.md` (on `wip/discard-policy-gate-2026-09-23`;
not yet on the main branch) during the Bruna onboarding, Stage 5i.
**Status: AI-AUTHORED, PENDING USER REVIEW.** Shipped default-on in the same change (the Pirates
onboarding precedent) so the rollout -- which always sheds heuristically -- and the verify gate see
the deck's own buckets from the first measurement rather than max-MV. Every choice below is open
to amendment; adoption is a user review, the same gate as cast order.

* Code: `BrunaProvider::CleanupDiscardCandidates` (`src/ai/DecisionProviders.cpp`, search for
  `---- BrunaProvider::CleanupDiscardCandidates`).
* Flag: `MTG_BRUNA_BUCKET_DISCARD` -- default ON via `EnvOn(..., true)`; `=0` restores
  `GenericProvider::CleanupDiscardCandidates` (the max-MV fallback) as the A/B hatch.
* Tests: `test/unit/test_bruna.cpp` (discard cases).

## 1. The deck, and therefore its shape

Bant voltron-ramp. 60 cards: 23 lands (incl. 3 Azorius Chancery Karoos, 3 Remote Farm, Skycloud
Expanse filter), 8 accelerants (2 Birds, 3 Avacyn's Pilgrim, 4 Somberwald Sage, Sol Ring, Wild Growth
-- Sage's 3 mana is creature-only), the KILL pieces (3 Bruna; payload Auras 4 Eldrazi Conscription
+10, 4 Colossification +20 [taps its host on entry], 2 Mythic Proportions +8, 1 Prodigious Growth
+7; 3 Lightning Greaves for haste; 2 Arcanum Wings, whose {2}{U} swap cheats a hand Aura onto the
host), 2 Mother of Runes (a 1/1 host), and 6 tutors (4 Glittering Wish -> a multicoloured sideboard
card: Bruna / Indrik Umbra / Almost Perfect / Unflinching Courage; 2 Open the Armory -> Aura or
Equipment). It needs (a) 6-8 mana for the payloads and Bruna, (b) ONE big Aura on ONE creature that
can attack this turn (haste or a turn on board), and (c) colours W, U, G.

**Shape: ramp + a split kill.** MANA is split LANDS / ACCEL. The KILL is not one combo piece but
four roles that must coexist -- payload, carrier, haste, (gatherer) -- so each gets its own 1-quota
bucket ("combo decks want each part split"). Tutors, Wings and extra payloads are the overflow,
value-ordered.

**Bruna changes the economics of an Aura in hand.** Her attack trigger puts every Aura card from the
hand AND THE GRAVEYARD that could enchant her onto her. So with Bruna on our battlefield a hand
payload is graveyard-equivalent -- shedding it costs nothing -- and that is the policy's one state
promotion.

## 2. Card-by-card role table (all read from `cards.json`)

| card | n | cost | key `params` | bucket / overflow value |
|---|---|---|---|---|
| Bruna, Light of Alabaster | 3 | {3}{W}{W}{U} 5/5 legendary | `attack_gather_auras` | KILL/GATHERER (1, net of board); extra copy 50; DEAD (S0) with one on board |
| Colossification | 4 | {5}{G}{G} | `is_aura`, `aura_power_bonus 20`, `aura_etb_tap_host` | KILL/PAYLOAD -- 80 |
| Eldrazi Conscription | 4 | {8} | `is_aura`, `aura_power_bonus 10` | KILL/PAYLOAD -- 60 |
| Almost Perfect (SB) | -- | {4}{G}{W} | `aura_set_base_power 9` (read as +8) | KILL/PAYLOAD -- 56 |
| Mythic Proportions | 2 | {4}{G}{G}{G} | `aura_power_bonus 8` | KILL/PAYLOAD -- 56 |
| Prodigious Growth | 1 | {4}{G}{G} | `aura_power_bonus 7` | KILL/PAYLOAD -- 54 |
| Indrik Umbra (SB) | -- | {4}{G}{W} | `aura_power_bonus 4` | KILL/PAYLOAD -- 48 |
| Unflinching Courage (SB) | -- | {1}{G}{W} | `aura_power_bonus 2` | KILL/PAYLOAD -- 44 |
| Lightning Greaves | 3 | {2} | `equip_grants_haste` | KILL/HASTE (1, net of board); extra 40 |
| Arcanum Wings | 2 | {1}{U} | `aura_swap_cost {2}{U}` | overflow 64 (30 once one is on board) |
| Glittering Wish | 4 | {G}{W} | `tutor_to_hand`, `wish_from_sideboard` | overflow 70 |
| Open the Armory | 2 | {1}{W} | `tutor_to_hand`, `tutor_types [Aura, Equipment]` | overflow 70 |
| Mother of Runes | 2 | {W} 1/1 | (protection deferred) | KILL/HOST when no creature exists; else 35 |
| Sol Ring | 1 | {1} | `mana_rock`, `produces_amount 2` | MANA/ACCEL (first) |
| Birds of Paradise | 2 | {G} 0/1 | `produces` WUBRG | MANA/ACCEL (also a host) |
| Avacyn's Pilgrim | 3 | {G} 1/1 | `produces` W | MANA/ACCEL (also a host) |
| Wild Growth | 1 | {G} | `is_land_aura`, `land_aura_extra_mana 1` | MANA/ACCEL |
| Somberwald Sage | 4 | {2}{G} 0/1 | `produces_amount 3`, `creature_mana_only` | MANA/ACCEL (last: its mana pays Bruna, never an Aura) |
| Azorius Chancery | 3 | land | `etb_bounce_land`, 2 mana W/U | MANA/LANDS; BLANK (S2) with no other land |
| Remote Farm | 3 | land | 2 W, enters tapped (depletion) | MANA/LANDS |
| Seaside Citadel | 2 | land | G/W/U, tapped | MANA/LANDS |
| Botanical Sanctum / Razorverge Thicket | 4 / 3 | land | fastlands | MANA/LANDS |
| Skycloud Expanse | 1 | land | `ramp_filter` W/U | MANA/LANDS |
| Forest / Boseiju | 5 / 1 | land | G | MANA/LANDS |

**Genuinely ambiguous:** (1) whether a tutor (70) should outrank Colossification (80) as overflow --
a tutor is any piece, but costs {2} and a turn; (2) Almost Perfect's value (its +8 assumes a 1-power
host; on Bruna it is +4); (3) Mother of Runes vs a dork as the HOST.

## 3. Buckets, quotas (all net of board)

"Mana" = mana for ANY spell (lands' `produces_amount`, rocks, unrestricted dorks, a land Aura's
extra). The payloads are non-creature spells, so Somberwald Sage's creature-only mana is 0 here.

| bucket | quota | netted against the battlefield by |
|---|---|---|
| MANA/LANDS | keep while board + kept mana < **7**, at most **3** lands; the first **2** taken before the KILL quotas, the third after | lands, rocks, unrestricted dorks, Wild Growth |
| MANA/ACCEL | **1** while board + kept-land mana < **6** (fungible: a dork-less hand keeps its lands) | same census |
| KILL/GATHERER | **1** while no gatherer on the battlefield | a Bruna we control |
| KILL/PAYLOAD | **1** (+1 more while no gather path: no Bruna on board or kept); **0** with Bruna on board (graveyard-equivalent) | each payload Aura already on the battlefield fills a slot |
| KILL/HASTE | **1** while none on the battlefield | a Greaves we control |
| KILL/HOST | **1** creature while no creature is on the battlefield or kept (a kept dork or Bruna counts) | any creature we control |

## 4. Within-bucket order and distance-to-playable

* Lands (keep order): colour coverage of what the hand needs (W, U, G pips) not yet on board or kept,
  then `produces_amount` (Chancery / Remote Farm first), then untapped.
* Accel (keep order): most any-spell mana (Sol Ring), then most colours (Birds), Sage last.
* Payloads: largest bonus first.
* Distance: a payload is FAR (-30) when its mana value >= reach + 2 (reach = board mana + every land
  and accelerant in hand) AND there is no gather path (Bruna) and no swap path (Wings on board or in
  hand) -- either path makes an Aura's mana cost irrelevant.

## 5. Total order over a hand (index 0 first)

S0 dead legend (Bruna with one on board / earlier in hand) -> S1 graveyard-equivalent payloads (Bruna
on board) -> S2 blank Karoos -> S3 surplus lands (reverse keep order) -> S4 surplus accel (Sage first)
-> S5 overflow, ascending value -> tail: quota-kept cards, last-taken first. Every non-staged hand
card is named, so the max-MV tier B never decides. The list goes through
`CleanupDiscardRankingWithOrder` (staged-card / required-piece protections stay engine-enforced).

## 6. Param predicates (as implemented)

land `CleanupDiscardIsLand`; karoo `etb_bounce_land`; accel `mana_rock || is_land_aura || (creature &&
!produces.empty())`; gatherer `attack_gather_auras`; payload `is_aura && !is_land_aura &&
!aura_swap_cost && (aura_power_bonus > 0 || aura_set_base_power >= 0)`; haste `equip_grants_haste`;
swap `aura_swap_cost`; tutor `tutor_to_hand`; host any other creature. No card name appears.

## 7. State promotions

Adopted: Bruna-on-board makes hand payloads graveyard-equivalent; the dead-legend demotion; the
Karoo blank. **Rejected as search-owned:** "this Aura is lethal next turn" (a damage projection),
which Aura Arcanum Wings should swap in (the provider's damage ranking at the swap site owns it),
and Colossification's tap-host timing (a cast-order / combat question).

## 8. Doubts, flagged for the user

1. Mana target 7 / max 3 kept lands. Conscription wants 8, but the 8th usually comes from the turn's
   own land drop; 3 kept lands is three turns of runway.
2. The graveyard-equivalence promotion sheds payloads AHEAD of surplus lands once Bruna is out. It
   is exact for the gather, but loses the option to hard-cast the Aura on a second creature if Bruna
   is somehow unable to attack (goldfish: never).
3. Tutors at 70 (below Colossification 80, above every other payload).
4. Second payload only when there is no gather path.
5. Real-play shed rate: see the measurement in `docs/design/analysis-bruna.md` (Stage 5i). The rollout
   sheds far more often than real play (Dragons: 10,020x), so `real ~ 0` does not make the rule inert.
