# Analysis ledger — Colossus Hammer (opened 2026-10-17)

## ⇥ RESUME HERE

**What this is.** The user dropped a new deck, `decks/Colossus Hammer/Colossus Hammer.cod`, into the
primary checkout (untracked there) on 2026-10-17 and asked to analyze it. Work happens on branch
`colossus-analysis` in the worktree `.claude/worktrees/colossus` (the primary checkout runs the play
viewer and is shared by other sessions, so nothing is edited there). The deck file was COPIED into the
worktree; the user's original stays in place.

**State:** Stage 2 research fanned out (8 Opus agents, one per card / card pair). Integration is serial.

## Standing decisions (pass to every subagent)

* No user strategy brief was given. Inferred plan: put **Colossus Hammer** (+10/+10, equip {8}) on a
  **double striker** (Swiftblade Vindicator natively; Kor Duelist while equipped; anything wearing
  Lizard Blades) **without paying equip {8}**: Sigarda's Aid (attach on enter), Magnetic Theft
  ({R}: attach target Equipment to target creature), Forge Anew (first equip each turn for {0}),
  Raubahn (attach on attack), Ardenn (begin-combat attach-all). Steelshaper's Gift / Open the Armory
  tutor the Hammer. Loxodon Warhammer's lifelink feeds Speaker of the Heavens (+7 life → 4/4 Angels).
* Goldfish frame: one passive opponent that never blocks, casts, or gains/prevents life. Evasion and
  trample are inert (approved precedents: Colossus Hammer "loses flying", Loxodon Warhammer trample).
* The engine makes no in-window greedy pick (CLAUDE.md). The deck gets its OWN provider
  (`ColossusHammerProvider`), which may only restrict/order options.

## List (main 60)

1 Ardenn, Intrepid Archaeologist · 4 Swiftblade Vindicator · 1 Raubahn, Bull of Ala Mhigo ·
4 Kor Duelist · 1 Forge Anew · 4 Sigarda's Aid · 2 Loxodon Warhammer · 4 Colossus Hammer ·
1 Dragonfire Blade · 4 Magnetic Theft · 2 Path to Exile · 4 Steelshaper's Gift · 3 Open the Armory ·
3 Clifftop Retreat · 4 Furycalm Snarl · 5 Mountain · 9 Plains · 3 Speaker of the Heavens ·
1 Lizard Blades.

Sideboard (6: Adelbert Steiner, Beloved Princess, Danitha Capashen, Outlaw Medic, Giott, Rabbit
Battery) is **unreachable** — no mainboard card fetches from outside the game — so coverage correctly
does not scan it.

## Stage 1 — coverage (2026-10-17)

**10 missing, 9 already full** (from KittyEquipment, carrying its approved deferrals): Ardenn, Kor
Duelist, Sigarda's Aid, Loxodon Warhammer, Colossus Hammer, Dragonfire Blade, Open the Armory,
Mountain, Plains.

Missing: Swiftblade Vindicator · Raubahn, Bull of Ala Mhigo · Forge Anew · Magnetic Theft · Path to
Exile · Steelshaper's Gift · Clifftop Retreat · Furycalm Snarl · Speaker of the Heavens · Lizard Blades.

Research split (Opus, read-only, return drafts): Lizard Blades · Raubahn · Forge Anew · Magnetic Theft ·
Speaker of the Heavens · Path to Exile · {Clifftop Retreat, Furycalm Snarl} · {Steelshaper's Gift,
Swiftblade Vindicator}.

## Stage 2 — cards

Full research drafts (verbatim agent reports) are kept under `logs/colossus/drafts/` in the worktree
(gitignored, host drive). Integration order: lands + simple cards → Lizard Blades → Magnetic Theft →
Forge Anew → Raubahn → Speaker → Path to Exile.

| Card | Tier | Status | Notes |
|---|---|---|---|
| Clifftop Retreat | 1 | integrated | `checkland_subtypes [Mountain, Plains]`; no C++ |
| Furycalm Snarl | 1 | integrated | `etb_untap_reveal_subtypes [Mountain, Plains]`; viewer `land_entry` already wired |
| Steelshaper's Gift | 1 | integrated | Open the Armory shape, `tutor_types [Equipment]` (matches Lizard Blades by subtype) |
| Swiftblade Vindicator | 1 | integrated | first PRINTED double striker in cards.json. Fixed the two equip-host rankers that read a HAND card's (empty placeholder) keyword mask — they now read the definition. Trample = approved-inert precedent |
| Lizard Blades | 3 | drafting | reconfigure is new |
| Magnetic Theft | 2/3 | drafting | Forge Anew's agent reports it is an INSTANT (verify) |
| Forge Anew | 3 | draft in | clause 3 = generalise Kili's per-plan waiver; **fix F**: the scalar mana bound credits a free equip only under metalcraft, so without Puresteel every "equip Colossus Hammer" plan is cut at {8} before the waiver is priced. Clause 1 needs a breakpoint (site 10) for "return Equipment → equip it this phase". Clause 2 (instant-speed equip) PROVISIONAL deferral |
| Raubahn | ? | drafting | |
| Speaker of the Heavens | ? | drafting | |
| Path to Exile | 2 | draft in, queued last | see below |

**Path to Exile decision (agent's call, surfaced to the USER).** The only live goldfish line is
SELF-target ramp (exile own creature → that controller fetches a tapped basic); exiling an opponent
spawn has no effect (spawns never attack/block, nothing reads them). Plan: model the resolution
faithfully in BOTH worlds (exile, controller land search, and the exile-branch DETACH bug fix — the
exile branch never reset `equipped_to`/`aura_attached_to`, unlike the tuck branch), widen the human
target chooser to both sides, keep self-target a SEARCHED cast, and prune only the opponent-target cast
in the provider (sound: no effect). NOT copied: KittyEquipment's `NeverCast(Removal)` (USER 2026-10-04,
"aren't relevant for Goldfishing") — it sits at the CollectActions choke point and would also hide Path
from the human in the viewer, and Path's self-target gives a land, unlike Swords' life.

## Provisional deferrals (await USER sign-off)

(none yet)

## Approved deferrals

(none yet)
