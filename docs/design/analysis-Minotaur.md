# Analysis ledger — Minotaur

Per-deck ledger for the `analyze-deck` run on `decks/Minotaur/Minotaur.cod` (started 2026-08-23,
run autonomously overnight at the user's request; user unavailable until Monday 2026-08-24).

Deck: 60 cards, Rakdos (B/R) Minotaur tribal aggro. 18 lands + 2 Aether Vial, 40 creatures/spells.

```
2 Boros Reckoner        3 Fanatic of Mogis      4 Rageblood Shaman     4 Kragma Warcaller
4 Deathbellow Raider    4 Ragemonger            4 Gnarled Scarhide     3 Neheb, the Worthy
4 Burning-Fist Minotaur 4 Slaughter-Priest of Mogis                    2 Sethron, Hurloon General
2 Aether Vial
5 Mountain  2 Blood Crypt  2 Rakdos Carnarium  4 Unclaimed Territory  4 Secluded Courtyard
1 Cavern of Souls  2 Bloodstained Mire
```

## Stage 1 — coverage (2026-08-23)

12 missing, 0 partial-with-gaps. Already implemented and reused: Blood Crypt, Mountain,
Unclaimed Territory, Secluded Courtyard, Cavern of Souls, Bloodstained Mire, Aether Vial.

Missing: Boros Reckoner, Fanatic of Mogis, Rageblood Shaman, Kragma Warcaller, Deathbellow Raider,
Rakdos Carnarium, Ragemonger, Gnarled Scarhide, Neheb the Worthy, Burning-Fist Minotaur,
Slaughter-Priest of Mogis, Sethron Hurloon General.

All 12 oracle texts fetched from Scryfall (`logs/minotaur_scryfall/`). **Slaughter-Priest of Mogis
was cross-checked through two independent Scryfall routes** (`/cards/named`, `/cards/thb/227`,
`/cards/search`) because the agent's recall disagreed with the API; the API text is authoritative
and the recall was wrong — the printed card is the sac-watcher/first-strike one, NOT an ETB pinger.

## Stage 2 — implementation status

| Card | Tier | Status |
|---|---|---|
| Rakdos Carnarium | 1 | done — Karoo (`enters_tapped` + `etb_bounce_land`, produces B/R x2) |
| Rageblood Shaman | 1 | done — `lord_effect` +1/+1 Minotaur, `lord_excludes_self` |
| Boros Reckoner | 1 | done — vanilla 3/3, hybrid `{R/W}{R/W}{R/W}` (3 red devotion) |
| Deathbellow Raider | 2 | done — `must_attack` |
| Kragma Warcaller | 2 | done — `grants_haste` + `attack_pump_matching_power` |
| Ragemonger | 2 | done — `reduces_subtype_colored_*` |
| Fanatic of Mogis | 2 | done — `etb_damage_devotion_color` |
| Neheb, the Worthy | 2 | done — `hand_size_anthem_*` + `combat_damage_each_discards` |
| Burning-Fist Minotaur | 2 | done — `firebreathing_*` + `firebreathing_discard` |
| Sethron, Hurloon General | 2 | done — `etb_token_includes_self` + `team_pump_*` (+haste) |
| Slaughter-Priest of Mogis | 2 | done — `sacrifice_watch_pump_power` + sac outlet |
| Gnarled Scarhide | 3 | done — `bestow_cost` alternate aura cast mode |

## PENDING USER APPROVAL — proposed deferrals (goldfish-inert collapses)

Per the analyze-deck skill every deferral needs explicit user sign-off. The user was asleep for
this run, so **each is implemented as an inert collapse and listed here for Monday review** rather
than being treated as silently approved. All of them belong to the one class the skill itself
names as legitimate ("provably changes nothing for goldfishing"): this engine's opponent never
blocks, never casts, never deals damage, and never targets our permanents (`Combat.cpp` has no
blocker code path at all — every declared attacker's damage goes straight to the face).

| # | Card | Clause | Why inert | Risk if wrong |
|---|---|---|---|---|
| D1 | Boros Reckoner | "Whenever this creature is dealt damage, it deals that much damage to any target" | Nothing in the sim ever damages our creatures (no blockers, no opponent spells, no self-damage source in this deck) | none — the trigger has no event that could fire it |
| D2 | Boros Reckoner | "{R/W}: gains first strike" | First strike is only observable against blockers | none |
| D3 | Rageblood Shaman | Trample (own + granted) | Trample only matters when a blocker absorbs damage | none |
| D4 | Neheb, the Worthy | First strike (own + granted to other Minotaurs) | as D2 | none |
| D5 | Burning-Fist Minotaur | First strike | as D2 | none |
| D6 | Gnarled Scarhide | "This creature can't block" / "Enchanted creature ... can't block" | We never block (we are the only attacker) | none |
| D7 | Sethron | "menace" rider on the activated pump | Menace only restricts blockers | none |
| D8 | Deathbellow Raider | "{2}{B}: Regenerate this creature" | Regeneration replaces destruction; nothing destroys our creatures in this sim | none |
| D9 | Slaughter-Priest | "gains first strike until end of turn" (the outlet's payload) | as D2 — the outlet is still implemented because its *cost* (sacrifice) triggers the card's own +2/+0 watcher, which IS live | none for the payload |
| D10 | Neheb | "each player discards a card" — the OPPONENT's half | The opponent never casts and their hand is never read by any decision | none |
| D11 | Boros Reckoner / Sethron | hybrid `{R/W}` and `{B/R}` render as their first colour in the play digest | Existing engine-wide convention (`ManaCost::ToString`), payment semantics unaffected | none |

Everything else on all 12 cards is implemented; there are **no Tier 1–3 clauses left unmodelled**.

## Stage 5 — verification results

| Check | Result |
|---|---|
| Stage 3 coverage (re-run) | CLEAN — 0 missing, 0 partial-with-gaps |
| 2d-bis `audit_card_costs.py` | **All mana costs match Scryfall.** All 11 costed Minotaur cards resolved and matched (66 unrelated pre-existing cards hit HTTP 429 rate-limit transients — none of them this deck's) |
| **Byte-identity for existing decks (smoke)** | **39/39 PASS**, 0 configs changed, 0 searched/d0 play changes |
| **Byte-identity for existing decks (regression)** | **65/65 PASS**, 0 configs changed, 0 play changes; viewer-protocol references 0 play-drift / 0 enum-gap |
| 5a nonconv (d3 b10 s2002 ×300, d5 b20 s3003 ×200) | **0 flagged** |
| 5a fd-diverge (d5 b20 s2002 ×300) | **0 flagged** |
| 5b multi-depth (500 games, seed 2002) | d0 5.464 → d3 4.996 → d5 4.994 — **monotone**, and a ~T5 kill is the right clock for a Rakdos tribal aggro deck |
| Devotion arithmetic | Hand-checked on 8 Fanatic casts against red pips — exact every time, including the self-count and same-turn cast ORDER |

## Verification log

- 2026-08-23: Stage 1 coverage run, 12 missing identified, Scryfall fetch complete.
- 2026-08-23: all 12 implemented; build + coverage clean; cost audit clean.
- 2026-08-23: smoke 39/39 and regression 65/65 byte-identical — the shared-code changes
  (ComputeLordBonus signature, the OnDragonEnters early-out, CanonicalSacVictim widening,
  EffectiveSpellCost, ApplyAttackSelfPumps, AttackWith, ResolveCombatDamage, PerformFetch)
  are provably inert for all 13 existing decks.
- 2026-08-23: 200-game observational sweep confirms Sethron tokens, both ActivatePump shapes,
  hybrid payment, and the cost reducer all fire in real games.
