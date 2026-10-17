# Analysis ledger — Colossus Hammer (opened 2026-10-17)

## ⇥ RESUME HERE

**What this is.** The user dropped a new deck, `decks/Colossus Hammer/Colossus Hammer.cod`, into the
primary checkout (untracked there) on 2026-10-17 and asked to analyze it. Work happens on branch
`colossus-analysis` in the worktree `.claude/worktrees/colossus` (the primary checkout runs the play
viewer and is shared by other sessions, so nothing is edited there). The deck file was COPIED into the
worktree; the user's original stays in place.

**State (2026-10-17 ~10:30 UTC; USER: "hold off on pushing ... stabilize everything" -- NOTHING PUSHED):**
`colossus-analysis` tip: see `git log`. Done and committed: all 10 cards; the two bp cast-record fixes
(`0f3c56499` Magnetic Theft's Equipment pin, `0d410db07` ten more fields); Stage 4 profile (`5e32c4d96`); Scryfall
snapshot (`7088c2f86`); ALL THREE TIERS with accepted GT (`8c1de4465`; suite_gate 0.43x PASS); the cast-order +
discard DRAFTS implemented OFF (`795642c39`) and MEASURED (`ac2095d02`: order -0.053 play / -0.056 d3 / -0.048 d0,
every slower game recovers; buckets neutral); viewer-audit state-gated class (5h now PASS); cost audit 435/435;
`5b39d898d` card-field audit green for EVERY deck (kicker/storied stripped as param-modelled, stale Glorybringer
entry dropped). verify_deck (`--no-network`, 10:01): every gate PASS except claude_sweep (6 unresolved = the fixes
in flight); claude-play sweep 18/18 recorded below.
IN FLIGHT (Opus fix agents, each in its own worktree under `.claude/worktrees/`, commit locally, no push):
`colossus-pay` (human-play multi-cast line payment, flag 3), `colossus-aid` (Sigarda's Aid host as a SEARCHED axis,
flag 2 -- changes Kitty play too), `colossus-fold` (reconfigure-then-equip illegal + waiver, dominated attach folds
incl. the double-Theft, Path human default; flags 4-6) -- these three off `ac2095d02`; off `5b39d898d`:
`colossus-idle` (an empty-handed pre-combat PASS gets no combat-pin variants -- Bruna gather / Primeval Titan /
Arcanum Wings, Raubahn already closed: a greedy pick in the window that moves OTHER decks) and `colossus-depskip`
(radix jump past equip-dependency-violated positions in the subset odometers; byte-identical, see Stage 5).
Ardenn searched axis: CANCELLED (USER ruling 2026-10-17, host picks may be heuristic -- see Standing decisions).
`colossus-idle` LANDED (idle pass through the sub-decision fan-out, 15 post-apply pins covered, smoke byte-identical;
gap fires only on mid-turn starts) and CONTINUED on (1) the executor dropping a breakpoint re-solve's after-apply pins
(lockstep) and (2) combat landfall targets always taking the front (no-greedy). `colossus-aid` REDIRECTED to the
heuristic host per the ruling. `colossus-fold` CONTINUED on the equip/reconfigure host identity fold;
`colossus-depskip` CONTINUED on the mana-walk cut.
NEXT: rebase each onto `colossus-analysis` (rebase, never
merge), build, units, scenarios, smoke + regression (`env -C`), per-deck verdicts on every play change, accept GT
with `--deck` filters (carry provenance notes), update the sweep flag count, then `leaf_tiebreak_check.py` (5c2) on
the final engine, verify_deck again, final report. Open USER questions: below + cast-order adoption, discard
buckets, human main-2 asymmetry, Ardenn's human pick.
GOTCHA: run `test/regression.sh` ONLY as `env -C <tree> bash test/regression.sh` — it uses the CWD's
binary/cards.json/results (a smoke run from the primary's cwd at 06:33 zeroed the primary's
`test/results/smoke.env`, a 10-14 leftover). Research drafts + integrator reports: `logs/colossus/drafts/`.

## Standing decisions (pass to every subagent)

* **USER RULING 2026-10-17 -- Sigarda's Aid host is a provider HEURISTIC.** *"The host should be handled the same way
  as kitty. Prefer doublestrike and creatures without summoning sickness."* / *"And just drop it on something."* /
  *"It can be done heuristically."* / *"Lizard Blades is also a creature replacement as needed."* Read as: ONE
  provider pick (no searched axis); rank double-strike POTENTIAL (native DS, Kor Duelist's while-equipped, a Lizard
  Blades wearer) first, then can-attack-this-turn; an entering Equipment always attaches (no decline); an unattached
  Lizard Blades is an ordinary (DS-tier) host candidate, and an entering Lizard Blades attaches only when some host
  gains double strike, otherwise stays a creature. Colossus provider only; Kitty keeps the base hook (diagnostic
  count of what the rule would move there is being collected). Answers Q7 and Q1's host half. The planned searched
  Ardenn axis is CANCELLED under the same ruling (Ardenn stays the provider's pick).

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
| Raubahn | 3 | integrated `312383bde` | `FireAttackAttachEquipment` (both combat worlds, before `FireAttackDigAttach`, declared Raubahn only): legal list = "attach nothing" + every moving pair (`AttachTargetEquipmentLegal` + is-attacking), sound folds only (no-op pairs; identical copies via the shared `AttachHostClassKey`); provider `AttackAttachCandidates` only ORDERS; SEARCHED pin `Plan::attack_attach_choice` → `GameState::scripted_attack_attach` (int16 in padding; sizeof(GameState) 1112; Plan 456→464); width W = 1 + E×H from the state. Through `ApplyEquip`; never sets `equip_activated_this_turn`; `atk_idx` re-found (an attached Lizard leaves combat). Projection `CountAttackAttachPump` = realised for every rank, with Ardenn. Fixed an idle-pass gap (a no-plan main took the front attach greedily) via `AppendIdleAttackAttachSiblings`. Viewer: `attach_host` with `pick` "equipment" (-1 = none, exempt from ONE_OPTION_AUTO) then "attacker" — verified claude-play s121. Ward PROVISIONAL. `MTG_ENUM_MEMO_VERIFY` 0 mismatches. 1330/1330 units; KittyEquipment/Bruna/Landfall byte-identical (20 g). **Cross-deck observation for the USER:** the same idle-pass gap exists for Bruna's gather pin, Primeval Titan's and the Arcanum Wings swap pins (not fixed); solve memo shows 2 pre-existing mismatches (identical on the parent) |
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

## Stage 4 — baseline profile (2026-10-17, `5e32c4d96`)

`analyze_deck.py --no-rebuild`: profile written (defaults / static keep; NO mulligan generation).
Cost diagnostic NO_COST_INTERACTIONS. Discard evidence (5i): 7 cleanup decisions in 400 games d3, every one
multi-optimal (STATUS_QUO_OK) -- the deck almost never discards, and never with stakes. `provider_audit.py
--check` rc=0: Colossus Hammer -> `ColossusHammer` (own provider, `cert=NotAssessed`). Scryfall snapshot:
the ten new cards added (`audit_card_fields.py --update`), no hard field mismatch on any deck card.

## Stage 4-bis / 5j — suite rows (2026-10-17)

All three tiers in one change: smoke d0 1000 / d3 150 b10 / d5 75 b20 (s1001) + `colossus2hg` d3 50 b10
canary (Speaker's gate reads the 2HG starting life); regression the same counts on s2002 + s3003; overnight
the ENTRY set (d0 4x2000, d3 4x400 b10, d5 4x300 b20 on 4004-10010). Probe (smoke rows, quiet box): d3 b10
1.21 s/game, d5 b20 1.93 s/game -- about Hinata's per-game cost. **`suite_gate.py --cost`: PASS, 0.43x**
(colossus 1,161 ms/game at its worst searched regression case vs pd 2,695, the reference; limit 3x).
GT ACCEPTED in all three tiers (filtered accepts, so every prior provenance note is carried; `check_gt_logs`
708 consistent / 0 stale): smoke d0 4.968 / d3 3.6467 / d5 3.6933 / 2hg d3 4.48; regression d0 5.166, d3
3.8533 / 3.6867, d5 3.7333 / 3.5867; overnight d0 5.026-5.075, d3 3.7125-3.8275, d5 3.72-3.8367 (makespan
2m23s on the PGO binary). Depth is monotone paired per game in every tier (d0 -> d3 -> d5) except ONE
game, overnight s7007 gi58 (d3 T3, d5 T4) -- see Stage 5.

## Stage 5 — verification findings

* **5b depth sweep, s1001 gi64: d0 won T3, d3 and d5 won T5 at EVERY budget (b10 .. unlimited, one digest).**
  Root cause: the breakpoint cast RECORD (rollout -> `AIEngine::replay_recorded`) carried Magnetic Theft's
  creature target but not its Equipment target (`Action::attach_equip_id`), so a Theft cast inside a
  continuation (Steelshaper's Gift finds the Hammer at site 10, the continuation casts it and Thefts it onto
  Kor Duelist) did nothing in the real game. FIXED `0f3c56499` + scenario
  `colossus_theft_in_tutor_continuation` (fails before). The same record also dropped TEN more fields the
  replay already forwards (devour, twobrid, land-put, loyalty, replicate, convoke, phyrexian, evoke,
  adventure, kicked) -- FIXED `0d410db07`. Full smoke: every deck byte-identical except Melira Pod gi3
  (normal + 2HG, still T4: its committed continuation convokes Chord of Calling, which the old replay could
  not pay and skipped). Full regression ALL PASS 166/166, play-changed=0, references 600 / 0 drift.

* **Budgeted search cost, s7065 gi58 (single thread, Release):** d5 unlimited 0.8 s / T3; b100 3.9 s / T3; b400
  3.9 s / T3 (budget not binding); b20 5.8 s / T4 (the one budget-starvation game of the depth sweep). The 5x is
  STRUCTURAL in shape -- unbudgeted play uses the lazy leaf (0 leaf solves), budgeted play solves a leaf at every
  node (10,955) -- and NOT the 2026-10-14 unbudgeted-only cuts (all four + the leaf memo off: unlimited still 0.9 s).
  Inside each leaf solve the subset odometer steps through equip-dependency-violated positions one at a time
  (perf: `SolveUncached` 21% self, `EquipPieceDepViolated` 6.8%); the copy predicate already has a radix jump, the
  dependency predicate does not. Charged no work units per position, so a jump is byte-identical -> `colossus-depskip`.
  **LANDED `e6418081c`** (MTG_EQUIP_DEP_SKIP, default ON; `=0` hatch; `_VERIFY=1` walks and counts unsound skips):
  jump rule = zero digits [0, L) and carry at L = max over violated requirements of min({g} u R), in all four walks
  (SolveUncached, EnumeratePlans, the flat fallback, Stage A). Verify 0 unsound over the full smoke manifest (358M +
  14M skippable positions); smoke 124/124 byte-identical (digests, avg AND units). s7065 gi58: instructions 0.783x,
  wall 0.940x; Kitty ~1.00x. NEXT collapse (same agent): ~13M of the ~15M positions the leaf walk still visits fail
  the scalar mana bound one at a time -- port EnumeratePlans' MTG_MANA_WALK_CUT to it.
  **LANDED (mana cut in SolveUncached's walk; MTG_MANA_WALK_CUT reused for the plain form, new MTG_MANA_GATE_WALK_CUT
  for the ManaGateIndex form, `MTG_MANA_WALK_CUT_VERIFY=1`):** verify 0 unsound over the full smoke manifest (659M +
  4.5M skippable positions); smoke 124/124 byte-identical (digests, avg, units). Instructions vs the committed tree:
  Colossus s7065 gi58 0.619x (with dep-skip: 10.89G -> 5.28G overall, 0.485x), Mirrorwing d3 0.977x, Snow/Kitty/burn/
  Knights within +-0.01%. Leaf walk now visits ~1.71M positions (was ~15M). Not pursued: `SubsetHasStrandedEquip` in
  consider() re-checks what the dependency digits already enforce (1.3% of instructions; sound only where the
  dependency map covers the frame).
* **Disk-full incident, ~11:00 UTC 2026-10-17:** the shared Docker disk hit 100% (66 MB free); the cause was the
  keep-table cache `~/.cache/mtg/keeptable` at 359 GB (one copy per worktree per deck -- it is keyed by tree path).
  The depskip agent deleted 913 entries (301.8 GB; regenerable cache, entries of the primary checkout, the colossus
  trees, live trees and anything < 24 h kept); disk now 68%, cache 46 GB. Only that agent's Profile build hit ENOSPC.
  OPEN (USER): key the cache by content or give it eviction, so worktrees stop multiplying it.

## Claude-play sweep
- commit: `8c1de4465` (engine identical through `ac2095d02`)
- seeds: 2100001..2100018 (one game each; the seed varies the game, `--game-index` does not) games: 18, Opus agents
- flags: 2 unresolved      # 3 FIXED 9674c1cb6; 4-6 FIXED (fold commit, rebased onto 4e823e60e); 1 = USER cast-order call; 2 in flight (colossus-aid)

Results (search d5 b200 vs Claude, win turn): 2100001 4/4, 2100002 2/2, 2100003 5/5, 2100004 3/3, 2100005 4/4,
2100006 5/5, 2100007 **7/2** (Claude kept a 1-lander seeing 6 draws; the static keep mulliganed to 5 -- a keep-table
question, not play), 2100008 4/4, 2100009 4/4, 2100010 4/4, 2100011 4/4, 2100012 2/2, 2100013 3/3, 2100014 **3/2**,
2100015 5/5, 2100016 3/3, 2100017 6/6, 2100018 4/4. Claude never slower.

Flags, verified against cards.json + the code:
1. **CONFIRMED -> FIXED by the order draft, OPEN under the shipped order** (2100014): under the generic order Sigarda's
   Aid ties every Equipment at rank 20 and plan order casts the Equipment first, so "Aid, then Hammer" is unfindable
   at any budget (T3 vs T2). The AI-drafted order (default OFF, USER's call) restores it. See the cast-order draft.
2. **CONFIRMED, autonomous** (2100011): Sigarda's Aid's host is ONE provider pick (`EquipmentEtbAttachHost`) that
   ignores whether the host can attack -- Hammer onto a sick Lizard Blades over a ready Swiftblade, T4 kill lost. A
   greedy pick in the search window. USER RULING 2026-10-17: a heuristic pick is fine -- rank DS potential, then
   can-attack, always attach (Standing decisions). IN FLIGHT on `colossus-aid` as that heuristic.
3. **CONFIRMED, human play** (2100016, 2100017): committing a multi-cast line taps the only source of a colour the
   rest of the hand needs (Snarl for {1}{R} while a Mountain stays up; the only Mountain for a generic {1}).
   **FIXED `9674c1cb6`** (MTG_HUMAN_PREPAY_DEMAND, default ON, human play only): the whole-line batch prepay
   (`BatchPrepayMainCasts` -> `TapForCostBacktrack`, battlefield order, hand-blind) now first holds one source per
   coloured pip the rest of the hand still owes, releasing holds before any existing hold class. Smoke byte-identical,
   refs 600 / 0 drift (Eldrazi s6 gi5 repaired index 1152 -> 1144: 8 duplicate menu entries merged, same line).
   Scenarios `colossus_human_prepay_keeps_{snarl,mountain}`. Design note `docs/design/prepay-hand-demand.md`.
   OPEN (raised to the USER): autonomous play uses the same hand-blind batch payer -- a line whose main-2 / post-draw
   cast needed a different tapping could be undervalued (plausible, unproven; mana payment is a standing exemption,
   so changing it is a quality A/B, not a no-greedy fix).
4. **CONFIRMED** (2100003): "reconfigure Lizard Blades onto X, then equip the Hammer onto Lizard Blades" is offered;
   the equip has no legal target but still consumes Forge Anew's first-equip waiver. **FIXED** (fold commit): an equip onto
   a non-creature host on our battlefield is never activated (nothing paid, waiver not spent, both worlds:
   `EquipTargetNotACreatureNow`), and the subset filter drops the reconfigure-then-equip plan (`SubsetHasEquipOntoAttachedPiece`).
5. **Width** (2100003, 2100017, 2100018): dominated no-op attach sequences (a Theft whose host reconfigures away; a
   Theft a later equip overwrites, on a same-plan hand-cast Equipment; ONE hand Hammer Theft'd twice). **FIXED** (fold
   commit): the Theft folds now cover a hand piece whose identity is pinned (exactly one same-name cast, none entered
   this turn); s2100003 T5 menu 173 -> 65 plans, every dropped plan has a smaller twin on the menu. Smoke: only
   Colossus moved -- searched 0 slower / 0 faster / 9 same-turn (the double-Theft line replaced by its cheaper twin), d0
   1 faster (gi448 T7 -> T6). Follow-ups in `docs/design/colossus-attach-fold-followups.md`; the equip/reconfigure
   host identity fold LANDED `335e01fa8` (MTG_EQUIP_HOST_FOLD, plan-level canonical relabelling so "one on
   each" survives; play byte-identical, work -0.3% d3); the Theft creature-class fold's completeness gap (two Thefts
   cannot put one Equipment on each of two identical creatures -- unfindable at any budget) is IN FLIGHT on the same agent.
6. **CONFIRMED, human default only** (2100005, 2100017): Path to Exile's preselected target ranks by printed power
   (Equipment ignored), so it defaults to the Hammer carrier. **FIXED** (fold commit): `RemovalHumanDefaultTarget` ranks by
   `EquipGatePowerOf` (s2100017: default moves from the 13/13 Lizard Blades to Speaker).
7. DISMISSED as by-design, raised to the USER (every agent): human play gets a FULL post-combat main
   (`AIEngine.cpp` play_this_phase `|| use_external`), the search only Speaker's narrow one -- so a hand-played
   reference can use a post-combat Theft re-host / reconfigure the search cannot express. See USER question 5.
8. Raised to the USER (2100006, 2100010): Ardenn's begin-combat attach -- a human's host pick can move NOTHING (the
   provider still picks the pieces by this combat's damage; it would not move the Hammer onto a lifelinking Speaker
   on a non-lethal turn). The prompt also appears with no Equipment/Aura out. Under USER question 4.
9. Cosmetic / human defaults (not fixed): the folded Steelshaper's Gift / Open the Armory menu entry and its
   tutor_etb default name the first-enumerated variant (Lizard Blades / Dragonfire / Warhammer) even when the Hammer
   is lethal; two same-name permanents carry identical labels; two Sigarda's Aids fire ONE attach trigger (CR 603.2
   says each triggers -- outcome-inert here: only the final host matters and nothing reads an attach event).

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
   -> ANSWERED by the 2026-10-17 host ruling (read as covering Ardenn's target too): Ardenn stays a provider pick.
5. **Magnetic Theft** — PROVISIONAL folds: targets collapsed to our own Equipment / creatures; instant
   timing outside the main phases collapsed. Post-combat re-host (move the Hammer onto a summoning-sick
   host for next turn) needs a second main — not enabled.
6. **Speaker of the Heavens** — narrow restricted post-combat main (only Speaker's token activation)
   `MTG_SPEAKER_M2`, default ON provisionally (main-1 vs main-2 is the USER's call).
7. **Lizard Blades** — reconfigure stays main 1 (no second main); Sigarda's Aid DECLINES to attach an
   entering Lizard Blades when no host gains (keeps its double-strike body) — a provider heuristic.
   -> ANSWERED 2026-10-17 (*"Lizard Blades is also a creature replacement as needed"*): keep the decline for an
   entering Lizard Blades when no host gains; every other entering Equipment always attaches.
8. **Cast order** — to be AI-drafted and presented (USER 2026-10-14: the generic order is never OK).
9. **Discard buckets** — to be AI-drafted and presented (analyze-deck 5i).

## Provisional deferrals (await USER sign-off)

See "Open USER questions" above — every deferral there is PROVISIONAL until signed off.

## Approved deferrals

(none yet)
