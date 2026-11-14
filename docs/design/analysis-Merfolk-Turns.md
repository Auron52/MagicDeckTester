# Merfolk Turns analysis checkpoint

Started 2026-11-14. Source deck: `decks/Merfolk Turns/Merfolk Turns.cod`
(copied unchanged from the user's untracked file in the primary checkout).

## Coverage

Initial coverage reported 11 missing cards. After the Deeproot Pilgrimage
checkpoint, `scripts/analyze_deck.py --coverage-only` reports 10 missing:
Emperor Mihail II, Stonybrook Banneret, Silvergill
Douser, Wanderwine Prophets, Svyelun of Sea and Sky, Merfolk Sovereign,
Rishadan Dockhand, Rogue's Passage, Seahunter, and Tidal Warrior.
Three Tree City is present but its chosen subtype and mana color are hardcoded
to Goblin and red; this is incorrect for Merfolk Turns and is a required gap.
All twelve Oracle entries were fetched from the live Scryfall API on 2026-11-14.

| Card | Critical rule and viewer choice |
|---|---|
| Deeproot Pilgrimage | One token per event in which one or more nontoken Merfolk become tapped; attacks, activated abilities, and mana taps all matter. Token is blue 1/1 Merfolk with hexproof. |
| Emperor Mihail II | Controller may privately look at the library top any time and cast Merfolk spells from it under normal timing and mana rules; each Merfolk cast offers optional `{1}` for a token. |
| Stonybrook Banneret | Reduces generic cost of Merfolk **or** Wizard spells once per Banneret; islandwalk. |
| Silvergill Douser | `{T}` targets a creature for `-X/-0` (Merfolk and/or Faeries controlled). Even when power reduction is inert against the passive opponent, tapping it feeds Pilgrimage. Human target choice. |
| Wanderwine Prophets | Champion another Merfolk on entry, linked return on leaving; combat damage offers a Merfolk sacrifice for an extra turn. Human champion and sacrifice choices. |
| Svyelun of Sea and Sky | Attack draw, conditional indestructible with two other Merfolk, ward `{1}` to other Merfolk. |
| Merfolk Sovereign | Other controlled Merfolk +1/+1; `{T}` targets a Merfolk for unblockable. Tapping feeds Pilgrimage; human target choice. |
| Rishadan Dockhand | Islandwalk; `{1},{T}` taps any target land. Tapping feeds Pilgrimage; human target choice. |
| Rogue's Passage | `{T}: {C}`; `{4},{T}` targets a creature for unblockable. Human target choice. |
| Seahunter | `{3},{T}` searches for a **Merfolk permanent** and puts it onto battlefield, then shuffles. Human library choice. |
| Tidal Warrior | `{T}` turns target land into an Island until end of turn. Can fix own colored mana; human land choice. |
| Three Tree City | Choose creature type as it enters; `{2},{T}` chooses one color and adds mana equal to own creatures of that type. The current hardcoded Goblin/red text is false for this deck. Human type and color choices. |

## Current stage

Stage 2: implement the missing cards and repair Three Tree City. The critical
play interactions are Prophets' champion and extra turn, Seahunter's library
put, Mihail's top-library casts and paid token trigger, Deeproot's tap trigger,
and Tidal Warrior's temporary Island effect. Do not run or accept an analyzer
profile with these missing.

### Deeproot Pilgrimage checkpoint

Card definition and the blue 1/1 hexproof Merfolk token are modeled. The shared
combat core treats simultaneous attacking Merfolk as one tap event. The unified
payment entry recognizes each committed nontoken Merfolk mana tap after payment
trials finish; the manual tap route recognizes each explicit mana activation.
The shared `ResolveDeeprootPilgrimageTap` helper is available for the remaining
Merfolk `{T}` abilities as they are implemented: call it once after a successful
activation that tapped a nontoken Merfolk, including Douser, Sovereign, Dockhand
and Tidal Warrior. Seahunter is Human, so its tap does not trigger Pilgrimage.
Focused unit checks cover simultaneous attackers, token/vigilance exclusions,
multiple Pilgrimages, committed mana taps and manual tap.
`./build.sh` passed and the focused doctest run passed (3 cases, 36 assertions).
`python3 scripts/audit_card_fields.py` passed all hard card fields. This
checkpoint leaves the four future Merfolk `{T}` activation hooks for their
card implementations; no such activation currently exists in the engine.

### 2026-11-14 integrator checkpoint

The 60-card source list is unchanged. Live Scryfall Oracle data and mana costs
were fetched for all eleven missing cards and Three Tree City. Deeproot
Pilgrimage is the first implemented card; the other ten card definitions and
their mechanics plus Three Tree City remain open. The
parent agent explicitly owns full regression, GT regeneration and the play
server, so those are excluded from this worktree's verification.

## Verification owed

After coverage becomes clean: full build/unit tests, analyzer baseline profile,
provider routing audit, cost gate, and Stage 5 correctness/viewer checks.
The parent agent owns smoke/full regression, overnight tier rows, GT, and the
Codex-play sweep per the explicit worktree assignment.
