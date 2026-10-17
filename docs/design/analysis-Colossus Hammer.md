# Analysis ledger — Colossus Hammer (opened 2026-10-17)

## ⇥ RESUME HERE

**What this is.** The user dropped a new deck, `decks/Colossus Hammer/Colossus Hammer.cod`, into the
primary checkout (untracked there) on 2026-10-17 and asked to analyze it. Work happens on branch
`colossus-analysis` in the worktree `.claude/worktrees/colossus` (the primary checkout runs the play
viewer and is shared by other sessions, so nothing is edited there). The deck file was COPIED into the
worktree; the user's original stays in place.

**State (2026-10-17 ~06:40 UTC):** 9/10 cards integrated. Chain A (`colossus-analysis`, this tree):
base `5fad63a1d` → Lizard Blades `84feaa456` → pin `bbace743e` → Magnetic Theft `b8f6e1e4c` → Forge Anew
`51533fbb8`; **Raubahn integrator RUNNING in this tree** (brief + draft + 3 prior reports). Chain B
(`colossus-b`, `.claude/worktrees/colossus-b`) rebased onto `51533fbb8`: Speaker `4906508f6` → Path
`d2f56aecb` → test fix `ddaa8771d` — 1338/1338 units. **Smoke tier RUNNING on chain B's tip** (from
`env -C <colossus-b>`; scenarios 209/209) — byte-identity check for every other deck.
NEXT: (1) read the smoke audit (expect play-changed=0 for every deck but this one; any move = bisect
the 7 commits); (2) when Raubahn lands, rebase colossus-b onto it, build, units, fast-forward
`colossus-analysis` to it; (3) implement the DRAFT cast order + discard buckets in ColossusHammerProvider
behind DEFAULT-OFF levers (`MTG_COLOSSUS_ORDER`, `MTG_COLOSSUS_BUCKET_DISCARD`; docs
`colossus-hammer-cast-order-draft.md`, `colossus-hammer-discard-policy-proposal.md`) and measure ON vs
OFF; (4) Stage 4: `analyze_deck.py --no-rebuild` profile, `provider_audit.py --check`, Scryfall snapshot
(`audit_card_fields.py --update`) + `audit_card_costs.py`; (5) Stage 4-bis/5j: rows in all three tiers +
GT (timing probe first, `suite_gate.py --cost`); (6) Stage 5: verify_deck.py, nonconv/fd-diverge,
multi-depth, leaf_tiebreak_check, viewer audit 5h, claude-play sweep (~18 Opus agents, vary SEED).
GOTCHA: run `test/regression.sh` ONLY as `env -C <tree> bash test/regression.sh` — it uses the CWD's
binary/cards.json/results (a smoke run from the primary's cwd at 06:33 zeroed the primary's
`test/results/smoke.env`, a 10-14 leftover). Research drafts + integrator reports: `logs/colossus/drafts/`.

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
| Lizard Blades | 3 | integrated `84feaa456` | Reconfigure, both halves. Attach rides the Equip action priced by `EquipCostGenericNow` at {2} (no phantom free equip; load-time guard: reconfigure cost must be pure generic); never self-attach (`CanAttachEquip`/`ApplyEquip`). Under Puresteel metalcraft the GRANTED equip {0} is a real equip ability (CR 301.5c) — not on this list. `IsReconfigureAttach` = "not an equip ability": never waived, never sets `equip_activated_this_turn`. Unattach = `PermAbilityMode::ReconfigureUnattach` ({2}, live only while attached). `RefreshReconfigureCreatures` at the end of `ApplyEquip` and the devotion/turn-start/begin-combat chokepoints: attached → not a creature, loses creature subtypes, its own attachments fall off (CR 704.5n); unattached → creature again (sick only if it entered this turn). `equip_grants_double_strike` read in `HasDoubleStrikeFromEquipment` (deck trait `deck_has_equip_ds_grant`). Sigarda's Aid DECLINES an entering Lizard unless a host strictly gains (provider heuristic, disclose). Ardenn's planner never self-attaches. Verified in a real `--claude-play` run (attach `verb equip, equip_cost 2`; unattach an activation). 15 new units. Viewer unattach = click on the attached chip (`cast=Lizard Blades`; unambiguous with one copy). Known residual: ~20 host-link clear sites not individually patched — the refresh clears stale links at the chokepoints; only Path can remove a host here and it is covered |
| Magnetic Theft | 3 | integrated `b8f6e1e4c` | Scryfall: `{R}` **Instant** (verified). A real cast whose (Equipment, creature) pair is a SEARCHED plan axis: `Action::attach_equip_id` + `enchant_target`, `#MT` dedup key, sound identity folds only (Equipment: name + attachment + counters; host: trick key + tapped + what it wears), already-attached pair not offered (CR 701.3b). Same-plan targets allowed (`SubsetHasStrandedAttachSpell`, `SubsetHasMissingTrickTarget`); ordering is STRUCTURAL (`ApplyAttachSpellDependencyOrder`, last step of `ApplyPayableCastOrder[At]`), no Generic tier edit. One resolver `ResolveAttachTargetEquipment` (both worlds, re-checks at resolution, through `ApplyEquip`, never sets `equip_activated_this_turn`); legality `AttachTargetEquipmentLegal`. Provider hook `AttachSpellPairCandidates` (no narrowing anywhere). Human: two `target` board clicks (`g_play_loyalty_chooser`), searched pair preselected — verified in claude-play (seeds 22, 31). PROVISIONAL: own pieces only; instant timing outside our main phases; post-combat re-host needs a second main. 1304/1304 units. Sanity: 20 g d3/b20 s7 4.50 → 3.80 |
| Forge Anew | 3 | integrated `51533fbb8` | Clause 3: ONE shared predicate `FirstEquipWaiverOnBattlefield` (Forge Anew always; Kili with an enduring story; neither once `equip_activated_this_turn`) read by the credit, the payment id and the frame scan; a same-plan Forge cast is credited (casts resolve before the trailing equip pass — verified). **Fix F** (scalar mana bound: + the largest non-reconfigure equip cost while the waiver is live and metalcraft is not — an upper bound) — proven by a test: free Hammer equip on 3 lands with Forge (and with Kili), not without. Fix G, and fix I (the greedy leaf `Solve` gets the battlefield-only credit — exact). Reconfigure never waived/never spends it; Theft never spends it. Clause 1: `ResolveEtbReturnGyEquipment` in the shared own-ETB cascade (mandatory when legal; full enter cascade, so Sigarda's Aid attaches it); SEARCHED pick, one variant per graveyard Equipment name (`#FE` key); unpinned base via provider `GyEquipmentReturnPick`; human = `dig` chooser (mandatory). Rollout lockstep fix N (pin passed in the noncreature-permanent enter branch; byte-identical for existing cards). `GyR_EquipmentNames` reader bit folded into BuildSimKey + Dominance. **Breakpoint site 10** (returned Equipment → equip it this phase) wired at every Courser site in both worlds, with the `PlanOpensBreakpoint` clause — shown load-bearing (removing it fails the d2 search test). Clause 2 (instant-speed equip) PROVISIONAL deferral. Deck traits `deck_has_gy_equipment_return`, `deck_has_first_equip_waiver`; sizeof(GameState) unchanged. Interleaved timing vs base: no slowdown. 1318/1318 units |
| Raubahn | ? | drafting | |
| Speaker of the Heavens | 2 | integrated `40c8b7837` (colossus-b) | Sliver Hive's `tap_token_*` family at cost `{0}`; shared `TapTokenSourceLive` (untapped + CanTapNow + life >= StartingLife()+7, so 2HG needs 37) at enumeration and both applies; white 4/4 flying Angel token. Restricted post-combat main on ColossusHammerProvider (`MTG_SPEAKER_M2`, heurarm `SPEAKER_M2`, default ON, PROVISIONAL). Fixes found on the way: (a) Basri's exert-Cat activation was offered while summoning sick in human play (now gated; autonomous never enumerated it); (b) the attack projection still counted a creature that had tapped for `TapForTokenPay` as attacking; (c) `EquipTargetColorCount` read tokens as colourless, so Dragonfire Blade's per-colour discount was wrong on a token host. 1285/1285 units; report `logs/colossus/drafts/report_speaker.md` |
| Path to Exile | 2 | integrated `8a950cdb3` (colossus-b) | One shared resolver `ResolveCreatureRemovalAt` in both worlds (exile, detach, Swords' rider, then `land_search_for_removal_controller` → `PerformLandSearch` for an OWN target: 1 basic, tapped, shuffle; -1 declines with no shuffle). Self-target SEARCHED: one variant per class of identical own creatures (`RemovalSelfTargetFoldKey` compares every per-permanent field + attachments by content; the k-th Path names the k-th class member so "exile both" stays findable); basic rides `land_search_pins`. Opponent target: one variant, refused by `ColossusHammerProvider::OffersRemovalCast` (bypassed in human play and by `MTG_UNPRUNE=uacast`). Fixes: (a) the exile/graveyard removal branch never detached Equipment/Auras (both worlds; reachable before only by Swords' human self-target); (b) the plan dedup keyed on card name only, so every Path target collapsed to the first emitted — `#PX<target>` term added; (c) the attack projection now drops a self-Pathed creature. 1292/1292 units. **Cross-deck observation for the USER:** the integrator reports the solo-trick `enchant_target` axis has the same name-only collapse (the code's own comment says trick targets fold to the best-first pick) — not touched here. Merge TODO: call `RefreshReconfigureCreatures` right after the detach in `ResolveCreatureRemovalAt` once chain A lands |

**Path to Exile decision (agent's call, surfaced to the USER).** The only live goldfish line is
SELF-target ramp (exile own creature → that controller fetches a tapped basic); exiling an opponent
spawn has no effect (spawns never attack/block, nothing reads them). Plan: model the resolution
faithfully in BOTH worlds (exile, controller land search, and the exile-branch DETACH bug fix — the
exile branch never reset `equipped_to`/`aura_attached_to`, unlike the tuck branch), widen the human
target chooser to both sides, keep self-target a SEARCHED cast, and prune only the opponent-target cast
in the provider (sound: no effect). NOT copied: KittyEquipment's `NeverCast(Removal)` (USER 2026-10-04,
"aren't relevant for Goldfishing") — it sits at the CollectActions choke point and would also hide Path
from the human in the viewer, and Path's self-target gives a land, unlike Swords' life.

## Integration plan (2026-10-17)

Base commit `5fad63a1d` on `colossus-analysis`: provider + routing + Keyword::Reconfigure + the three
signature params + Tier-1 cards + verified placeholder entries for the rest. Two serial chains, each one
Opus integrator per card working from `logs/colossus/INTEGRATOR_BRIEF.md` + its draft:

* **Chain A** (this worktree, branch `colossus-analysis`): Lizard Blades → Magnetic Theft → Forge Anew →
  Raubahn. Serial because each builds on the previous card's attach primitive.
* **Chain B** (`.claude/worktrees/colossus-b`, branch `colossus-b`): Speaker of the Heavens → Path to
  Exile. Independent code (tap-for-token, removal). Rebased onto A at the end.

Smoke / regression / multi-deck batches are run centrally by the orchestrator only (one heavy batch at a
time on the box).

## Open USER questions (provisional defaults TAKEN — the work continues on them)

1. **Provider** — `ColossusHammerProvider` derives from `DeckProvider` (Generic). It does NOT inherit
   KittyEquipment's user-settled hooks (cast order, discard buckets, equip-host width 2, consolidation
   doctrine, NeverCast(Removal), the equip-chain route). Is that right, or should any of them apply?
2. **Path to Exile** — opponent-target cast pruned (no effect); self-target ramp searched; NOT
   NeverCast (see Stage 2). PROVISIONAL deferrals: the opponent's "may search" is a no-op, instant timing
   collapsed to main phases, the "may" always taken. Main-1 vs main-2 for a self-target Path: main 1.
3. **Forge Anew** clause 2 (instant-speed equip during your turn) — PROVISIONAL deferral (no window in the
   engine adds damage; full argument in the card note).
4. **Raubahn** — Ward PROVISIONAL deferral (only an opponent's spell triggers it). The attack attach is a
   FULL-WIDTH searched combat pin (Bruna route), not a provider single pick. Should Ardenn's existing
   single-pick begin-combat attach get the same searched axis?
5. **Magnetic Theft** — PROVISIONAL folds: targets collapsed to our own Equipment / creatures; instant
   timing outside the main phases collapsed. Post-combat re-host (move the Hammer onto a summoning-sick
   host for next turn) needs a second main — not enabled.
6. **Speaker of the Heavens** — narrow restricted post-combat main (only Speaker's token activation)
   `MTG_SPEAKER_M2`, default ON provisionally (main-1 vs main-2 is the USER's call).
7. **Lizard Blades** — reconfigure stays main 1 (no second main); Sigarda's Aid DECLINES to attach an
   entering Lizard Blades when no host gains (keeps its double-strike body) — a provider heuristic.
8. **Cast order** — to be AI-drafted and presented (USER 2026-10-14: the generic order is never OK).
9. **Discard buckets** — to be AI-drafted and presented (analyze-deck 5i).

## Provisional deferrals (await USER sign-off)

See "Open USER questions" above — every deferral there is PROVISIONAL until signed off.

## Approved deferrals

(none yet)
