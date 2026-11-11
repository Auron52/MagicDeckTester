# Analysis ledger — Shadowborn Apostle

Deck: `decks/Shadowborn Apostle/Shadowborn Apostle.cod` (mono-B, splash R via 1 Blood Crypt):
18 Shadowborn Apostle, 4 Dark Ritual, 4 Thrumming Stone, 4 Diabolic Intent, 2 Profane Tutor,
1 Demonic Tutor, 1 Culling the Weak, 1 Lotus Bloom; Demons 1 Reaper from the Abyss, 1 Abhorrent
Overlord, 1 Sire of Insanity, 1 Jetmir, Nexus of Revels; lands 17 Swamp, 3 Lake of the Dead,
1 Blood Crypt. No sideboard.

The USER asked for the analysis on 2026-11-03 ("Let's analyze the new Shadowborn Apostle deck").

Branch/worktree: `shadowborn-analysis` @ `.claude/worktrees/shadowborn` (cut from origin
`phase-1-2-deck-analyzer`). The deck folder was COPIED from the primary checkout (untracked there;
original left in place). The primary's other uncommitted files belong to another session.

**The plan.** Cast Shadowborn Apostles (1 mana each; Thrumming Stone gives every spell ripple 4, so
each Apostle cast reveals the top four and casts every Apostle among them free, and each of those
ripples again). With six Apostles out, `{B}, sacrifice six Apostles` puts a Demon from the library
straight onto the battlefield. Rituals (Dark Ritual, Culling the Weak, Lake of the Dead, Lotus Bloom)
accelerate; the tutors find Thrumming Stone or a missing piece. Jetmir pumps a wide board, Abhorrent
Overlord makes devotion-many 1/1 flyers, Reaper and Sire are 6-power bodies.

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing clauses are inert, but
  self-binding clauses bind us (Sire discards OUR hand, Overlord sacrifices OUR creature, Reaper's
  mandatory "destroy target non-Demon creature" may have to hit OUR creature). Model the full card;
  goldfish-inert deferrals are PROVISIONAL until the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold, and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic).
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`.

## Stage 1 — coverage (2026-11-03)
Missing (12): Dark Ritual, Thrumming Stone, Diabolic Intent, Shadowborn Apostle, Sire of Insanity,
Lake of the Dead, Reaper from the Abyss, Abhorrent Overlord, Profane Tutor, Demonic Tutor, Jetmir
Nexus of Revels, Culling the Weak. Present (3): Swamp, Blood Crypt, Lotus Bloom (re-verify against
Scryfall in Stage 2). Scryfall JSONs: `build/scratch/shadowborn/scry/` (primary build volume).

## Stage 2 — cards (in progress)
Research fan-out launched 2026-11-03 (four Opus agents: apostle+sac-costs / ripple / demons /
mana+tutors). Drafts: `logs/shadowborn/research/*.md` (gitignored; decisions below are the durable
record).

### Research: apostle + sac costs (done 2026-11-03; draft logs/shadowborn/research/apostle.md)
- **Shadowborn Apostle — Tier 3.** New mode of the existing permanent-ability action. Cost modelled
  on Captivating Vampire's tap-five-Vampires (Apostles fold by attack-ready vs not; only the COUNT of
  attack-ready Apostles given up is searched). Fetch modelled on Birthing Pod (sac via the shared death
  cascade -> Demon onto battlefield with ETB -> shuffle). No {T} in cost: summoning-sick / tapped
  Apostles are legal. New `tutor_required_subtype` filter ("Demon creature card"). Second-main relevant
  (attack, then sac) -> `DeckUsesSecondMain` hook. Viewer: bucket A (plan variant for the Demon +
  `sacrifice` board clicks) + a new line verb.
- **Diabolic Intent / Culling the Weak — Tier 2.** Generic `sac_additional_creature` flag; one shared
  reader replaces ~20 checks of `sac_additional_creature_color`. LOCKSTEP BUG: the rollout pays the sac
  only inside Natural Order's branch; move it to the generic site. Culling needs its own victim
  variants + can't-cast-without-a-creature. The current per-name victim fold is lossy (oldest,
  attack-ready Apostle over a sick one) -> fold by attack-readiness instead.
- 4-copy limit is enforced nowhere ("any number" needs no code). Creatures-died-this-turn is NOT
  tracked (Reaper needs it).
- Provisional calls (surfaced to the USER, not blocking):
  1. Culling the Weak cast at main-phase timing only (none of the four goldfish instant windows gains
     anything). PROVISIONAL deferral.
  2. Apostle fail-to-find stays offered (as Birthing Pod; complete, and morbid/shuffle make it non-null).
  3. Victim-fold fix scoped to the new flag only; extending it to Natural Order (Stompy) is a play change
     needing its own A/B -> separate follow-up.
  4. Two activations in one turn: the Demon ORDER is searched (Overlord-first gives more Harpies via
     Apostle devotion); no hard-coded rule.

### Research: Thrumming Stone / ripple (done 2026-11-03; draft logs/shadowborn/research/ripple.md)
- **Tier 3.** CR 702.60: every cast triggers ripple, incl. free casts and casts off suspend (Lotus
  Bloom / Profane Tutor arrive at upkeep, before the draw). Ripple-cast spells ripple again; within a
  chain the cast spells resolve before older pending triggers (stack order). Free casts still pay
  ADDITIONAL costs (Intent / Culling sacrifice). Legend rule is an SBA, so at most ONE Stone is out at
  any cast -> never two ripple triggers per spell (the brief's premise was wrong).
- Folds: reveal-or-not is a REAL choice (bottoming the top four changes draws). Sound fold: for an
  Apostle/Ritual chain only the NUMBER of revealing triggers matters (2^k -> k+1). Before a tutor's
  shuffle, a reveal that casts nothing changes nothing (shuffle depends only on library contents).
  Other revealed same-name cards (Ritual, Stone, Intent, Profane) stay searched.
- Code gaps to fix: `CastOffSuspend` skips cast triggers; `PushFreeCast` does not pay additional sac
  costs; the rollout resolves chains depth-first (wrong stack order when one reveal casts two tutors);
  free-cast tutor targets / sac victims would be greedy picks inside the window -> must be searched.
- Viewer: reveal y/n reuses `dig` ("(ripple reveal)"); which-to-cast reuses `free_cast`, re-asked after
  each cast; bottom order would need a new chooser.
- Provisional calls (surfaced to the USER, not blocking):
  1. Cast-every-revealed-Apostle is NOT a strict dominance -> implemented as a budget-staged admission
     order (cast-all first; declining admitted in a later wave), so nothing is unfindable at b0.
  2. Bottom order = reveal order, PROVISIONAL deferral, with a counter of how often the bottom is ever
     reached/drawn.
  3. Ritual-chain mana: counted by the planner ahead of time (no mid-turn re-solve).
  4. No instant-speed interleaving inside a chain: PROVISIONAL deferral.
  5. Viewer reveal prompt ON by default (no silent auto-resolve); ~15 prompts per chain possible.

### Research: the four Demons (done 2026-11-03; draft logs/shadowborn/research/demons.md)
- Engine facts: the OPPONENT HAS CREATURES (`GoldFishRunner::PopulateOpponentSpawns`, T1-3 in 8 of 10
  game indices, no subtypes -> legal Reaper targets); Reaper must hit OUR creature when none is left
  (gi%10 in {0,1}, before the spawn turn, after a kill in single-spawn patterns). No
  creature-died-this-turn tracker -> one flag set in `OnCreatureDies`, cleared at both untap sites; all
  new sacrifices (Apostle sac-six, Intent, Culling) must route through `SacrificePermanentAt`. The
  opponent's turn is notional (no end step). Already present: `DevotionTo`, `CreateTokens`, a shared
  double-strike check, the Echo / Defense-of-the-Heart "plan pins next upkeep's choice" pattern.
  Flying/trample inert (no blocking); vigilance inert here (no {T} ability on any creature) but cheap to
  wire faithfully. Demons arrive summoning-sick. No Demon is second-main relevant.
- **Abhorrent Overlord — Tier 3.** `etb_tokens_per_devotion_color` in the shared ETB path; shared
  upkeep forced-sacrifice helper, victim SEARCHED and pinned by the prior turn's plan (new plan field);
  viewer reuses `sacrifice`.
- **Reaper from the Abyss — Tier 3.** End-step destroy helper on the morbid flag, searched over grouped
  targets (Abiding Grace end-step pattern); viewer reuses `target` (Shriekmaw).
- **Sire of Insanity — Tier 2/3.** End-step hand discard at the same point in executor and rollout;
  must skip staged/exiled cards. No choice.
- **Jetmir — Tier 3.** Tiered count anthem (3/6/9) in `ComputeLordBonus` + double strike / vigilance
  grants in the existing checks; creature count computed ONCE per board (avoid the board-width
  quadratic). Uncastable here; arrives via Apostle.
- Provisional calls (surfaced to the USER, not blocking):
  1. "Each end step" reduced to OUR end step for Reaper + Sire (nothing dies / our hand is empty in the
     notional opponent turn; Resplendent Angel precedent). Sire's opponent-end-step discard is still
     wired (cheap, faithful).
  2. Overlord sac victim and Reaper target stay FULLY SEARCHED, provider only orders. "Harpy before
     Apostle" is NOT a sound dominance (sick Apostle vs attack-ready Harpy) -> not folded.
  3. Flying / trample / vigilance disclosed as inert.

### Research: mana, lands, tutors, routing (done 2026-11-03; draft logs/shadowborn/research/mana.md)
- **Dark Ritual — Tier 1:** `ritual_floating_mana: 3` + `ritual_float_color: "B"`. A free ripple cast
  must still add BBB; the d0 ritual-payoff guard drops "surplus" rituals unless a `tutor_to_battlefield`
  card is in the plan (check it does not mis-drop here).
- **Demonic Tutor — Tier 1:** `tutor_to_hand`, no filter. COVERAGE DEFECT: generic tutor list is in
  shuffle order and the search scores only 6 names; this deck can want ~15 -> some targets unreachable
  at any budget. `ShadowbornProvider::TutorSearchWidth()` = 32 (Cyclone precedent), PROVISIONAL, A/B owed.
- **Profane Tutor — Tier 2:** suspend-only + {1}{B} suspend cost already work. Missing: a tutor branch
  in `CastOffSuspend`, and a target PIN chosen the prior turn (Search for Tomorrow upkeep-pin pattern) —
  else a greedy top pick inside the window. Viewer reuses the tutor prompt at upkeep. "You may cast it"
  collapsed to always-cast (PROVISIONAL). Ripple must hook `CastOffSuspend` too.
- **Lake of the Dead — Tier 3:** two params (ETB sac-a-Swamp replacement; {T}, sac a Swamp: BBBB).
  Tap-first rule (an untapped Swamp is tapped for mana before being sacrificed) makes identical Swamps
  interchangeable; basic Swamp before Blood Crypt (the only R source) — sound for this list. Playing Lake
  with no Swamp is omitted from the search (holding dominates) but offered in human play. Viewer reuses
  `sacrifice`.
- Re-verify: Swamp, Lotus Bloom match Scryfall; Blood Crypt cosmetic-only diff. Lotus Bloom's
  "sac colour not wired in viewer" note looks stale — 5h to confirm.
- **Routing:** `tutor_to_hand` would route to AntiLifegainProvider (and other new params could hit
  Dragonstorm/Goblins/Minotaur). New `ShadowbornProvider : DeckProvider`, top-priority signature OR-ing
  ripple + the Apostle param + Lake's param + suspend-with-tutor. Certificate NotAssessed.
- **Second main (USER-OWNED decision):** the deck wants it — sacrificing Apostles (activation, Culling,
  Intent) AFTER combat keeps their attack damage; Jetmir is the case where activating before combat
  wins. This is a combat-ordering harvest, not an expressibility hole. PROVISIONAL: enable via a
  `DeckUsesSecondMain` hook on the Apostle param (the 2c-bis route), measure at the matrix, USER decides.

## Stage 2 — integration plan (serial; one integrator at a time, each commits on shadowborn-analysis)
- INT-1: Dark Ritual, Demonic Tutor, generic `sac_additional_creature` (+ rollout lockstep fix +
  attack-readiness victim fold, scoped to the new flag), Diabolic Intent, Culling the Weak,
  creature-died tracker, Lake of the Dead, Profane Tutor (CastOffSuspend tutor + pin), ShadowbornProvider
  + routing signature + TutorSearchWidth.
- INT-2: Shadowborn Apostle activation + `tutor_required_subtype`, the four Demons, second-main hook.
- INT-3: Thrumming Stone / ripple (executor + rollout + planner + suspend hook + viewer).
- Then: 2d review, 2d-bis cost/field audits, Stage 3 coverage, Stage 4 profile, 4a, 4-bis, Stage 5.

## INT-1 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `42aadf696` (engine, cards, tests) + the ledger commit that adds this section.

**Cards (cards.json; cost + oracle verbatim from Scryfall, bracket notes appended):** Dark Ritual
(`ritual_floating_mana 3`, `ritual_float_color B`), Demonic Tutor (`tutor_to_hand`, any card), Diabolic
Intent (`tutor_to_hand` + `sac_additional_creature`), Culling the Weak (`ritual_floating_mana 4` / `B` +
`sac_additional_creature`), Lake of the Dead (`basic_land` B + `etb_replace_sacrifice_land_subtype Swamp` +
`sac_for_mana_amount 4` / `sac_for_mana_color B` / `sac_for_mana_victim_land_subtype Swamp`), Profane Tutor
(`suspend_time_counters 2`, `suspend_cost {1}{B}`, `tutor_to_hand`).

**New params:** `sac_additional_creature` (bool), `etb_replace_sacrifice_land_subtype` (string),
`sac_for_mana_victim_land_subtype` (string). All default-off; no existing card carries them.

**Engine:**
- Shared readers `HasSacCreatureAdditionalCost` / `SacCreatureCostFoldsByReadiness` /
  `SacCreatureCostLegacyRoute` (CardDatabase.h) replace every `sac_additional_creature_color` gate except
  the Stompy routing signature. The legacy colour route (Natural Order) is untouched: same victim fold,
  same rollout position, same same-plan axis (`MTG_SAC_CREATURE_AXIS`).
- Generic route: the rollout pays the sacrifice at the CAST site right after the hand erase (the
  executor already paid at its cast site), for every resolution shape (lockstep fix). A cast with no
  creature to sacrifice is dropped before any mana moves, in both worlds, and graded Illegal by CheckLine.
- Victims (CollectActions' SAC-ANY-CREATURE post-pass): battlefield classes from
  `SacReadinessVictimReps` (identity fold + readiness dominance: a strong copy, i.e. untapped and not new
  this turn, is not offered while a weak copy of the same identity exists; proofs in the code comment), plus
  one variant per distinct creature NAME in hand (`Action::trick_hand_target` = the co-cast requirement,
  `SubsetHasMissingTrickTarget`), so "cast an Apostle, then Culling it" is enumerable on an empty board.
  `ResolveSacReadinessVictimId` maps a hand pin onto the copy the plan cast. `ApplyAttachSpellDependency
  Order` now also keeps such a spell after its victim's cast.
- Morbid: `GameState::creature_died_this_turn`, set in `OnCreatureDies` for a dying CREATURE (the shared
  `SacrificePermanentAt` also routes land sacrifices there, hence the type check), cleared in
  `GameEngine::UntapStep` and `SimulateEndAndStartNextTurn`. Write-only today: no reader, no key fold.
- Lake of the Dead: the entry replacement in `PlayLandCore` (no Swamp -> graveyard, drop spent, no
  landfall); the crack as a branch of `ApplySacForMana` (source tapped and kept, another Swamp eaten);
  shared `ChooseSubtypeLandSacrifice` (identity fold + basic-subset dominance: basic Swamp before Blood
  Crypt), `FloatLandBeforeSacrifice` (factored out of the Karoo float, which keeps its lever) and
  `SacrificeLandAt` (attachments fall off, sacrifice watchers, never the creature death cascade). Cracks
  are emitted at most once per available victim. `LakeDropWithoutVictim` keeps every autonomous land
  choice off a Lake with no Swamp (human play still offers it). Viewer: the crack keeps a board chip
  (`sacout=Lake of the Dead`, "Lake of the Dead: tap, sacrifice a Swamp"), not an implicit mana source.
- Profane Tutor: `CastOffSuspend` tutor branch; its target is the packed upkeep pin stream Search for
  Tomorrow uses (one ordinal per search-bearing arrival). The upkeep-pin fan counts a tutor arrival
  (width = distinct fetchable names, capped by `TutorSearchWidth`) and opens on a deck without land search
  when such an arrival is due; fans (0)-(2) of that block stay gated on `deck_has_land_search`.
- `ShadowbornProvider : DeckProvider` (Certificate NotAssessed with what a proof must count;
  `TutorSearchWidth() = 32`, PROVISIONAL). Routed FIRST in `DetectDecisionProvider` by the `shadowborn`
  flag. **Expected params for INT-2 / INT-3 (add them to the same OR):** `sac_named_count > 0` (Shadowborn
  Apostle's "sacrifice six creatures named ~" activation) and `grants_ripple > 0` (Thrumming Stone). Both are
  named in the code comment at the flag.
- Viewer: `scripts/audit_viewer_decisions.py` MANIFEST + text-coverage rows for the three params
  (`sacrifice`); `tools/play/DECISIONS.md` rows (`sacrifice` reuse x2, `tutor` reuse for Profane at upkeep).

**Tests:** `test/unit/test_shadowborn_int1.cpp` (8 cases: routing + width; morbid set / land-sac not /
rollout clear; readiness fold; Culling emission with no creature / hand victim / board victim; hand-pin
mapping at payment; Lake victim choice + `LakeDropWithoutVictim`; Lake crack tap-first + no-victim no-op;
Profane upkeep pin). `test_pirates_provider.cpp` routing table pins `Shadowborn Apostle ->
ShadowbornApostle` (the table was failing since the Stage 1 commit added the deck folder). Scenarios (7):
`shadowborn_culling_same_plan_victim`, `..._unpaid`, `shadowborn_culling_no_creature_uncastable`,
`shadowborn_intent_sacrifices_board_creature`, `shadowborn_lake_tap_first_float`,
`shadowborn_lake_crack_sacrifices_other_swamp`, `shadowborn_profane_suspend_upkeep_tutor`. Unit suite
2433/2433, scenarios 398/398.

**Smoke:** `test/regression.sh --smoke` (not accepted): 161 passed, 4 failed. All four failures are Merfolk (merfolk d0/d3/d5, merfolk2hg d3), and they are NOT INT-1's: HEAD's own binary (aa2b668da, built in a scratch worktree) reproduces the same merfolk_smoke_d3 digest (16290806e85e1728, avg 4.44 vs GT 4.18). The Merfolk ground truth committed with ff7a14850 does not match its own tree. Every other deck is byte-identical (play-changed only on Merfolk). Flag for whoever owns Merfolk; nothing was accepted.

**Left for INT-2 / INT-3 / review:**
- INT-3: `PushFreeCast` must pay the additional sacrifice for a free Intent / Culling (and drop it with no
  creature); the readiness victim needs a searched pin there too. `CastOffSuspend` ripple hook.
- INT-2: Reaper's reader of `creature_died_this_turn` must add the state-key folds (BuildSimKey / dedup /
  breakpoint / dominance) gated on a `deck_has_morbid` stamp; the six-Apostle sacrifice must go through
  `SacrificePermanentAt`. The second-main hook (`DeckUsesSecondMain`) is INT-2's.
- 2d review: the same-plan-victim plan can be over-admitted by the flat subset math when the victim's cost
  would need the ritual's own float (Culling's {B}{B}{B}{B} credited against the creature it eats). The
  realisation drops the cast (`shadowborn_culling_same_plan_victim_unpaid` pins that no free cast happens),
  so it is a menu/search-waste gap, not an illegal line. A sequential-walk gate was not added because
  `SubsetPayableSequential` refuses any subset holding a SacForMana (the Lake crack), which would make real
  lines unfindable.
- Generality gap (unreachable for this list): `ChooseSubtypeLandSacrifice` takes the front class when its
  folds leave more than one (two non-basic Swamps of different names); no searched axis feeds it.
- CheckLine treats an UNDECLARED Lake crack like other SacForMana sources (implicit, added when the line
  needs the mana); the summary names it, so the human sees it. Revisit in 5h if it surprises the user.

**Provisional calls (surfaced, not blocking; USER sign-off owed):**
1. `TutorSearchWidth 32` for this deck (coverage fix; held-out A/B owed).
2. Profane Tutor "you may cast it" collapsed to cast (ledger call, implemented).
3. Culling the Weak at main-phase timing only (ledger call, implemented as a bracket note).
4. Victim-fold fix scoped to the new flag; Natural Order (Stompy) keeps the name fold pending its own A/B.
5. Lake of the Dead: basic Swamp before Blood Crypt as a sound dominance (disclosed caveat: a card that
   rewards basic-ness or reads graveyard lands would break it; none in this pool).

## USER sign-off (2026-11-03)
USER, replying to the list of provisional calls: *"Yes, those deferrals and calls sound reasonable for
now."* Covered (all now APPROVED for now, revisit-able):
- Culling the Weak at main-phase timing only.
- Apostle fail-to-find stays offered; Demon order across two activations searched.
- Better sac-victim fold scoped to the new flag only (Natural Order / Stompy untouched).
- Ripple: cast-all-revealed-Apostles first with declining in a later budget wave; bottom order = reveal
  order (with a counter); ritual-chain mana counted by the planner; no instant-speed interleaving inside
  a chain; viewer reveal prompt ON by default.
- "Each end step" reduced to our end step for Reaper + Sire (Sire's opponent-end-step discard still wired).
- Flying / trample / vigilance disclosed inert.
- Second main enabled for this deck via the Apostle param (to be measured at the matrix).
- `TutorSearchWidth()` = 32 (A/B still owed); Profane Tutor always cast off suspend; Lake sacrifices a
  basic Swamp before Blood Crypt.

## Approved deferrals
- Culling the Weak instant timing (main-phase only) — USER 2026-11-03.
- Ripple bottom order fixed to reveal order — USER 2026-11-03.
- No instant-speed interleaving inside a ripple chain — USER 2026-11-03.
- Profane Tutor "you may cast" collapsed to always-cast — USER 2026-11-03.
- Reaper / Sire "each end step" reduced to our end step — USER 2026-11-03.
- Flying / trample / vigilance inert in goldfish — USER 2026-11-03.

## INT-2 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `df316ac28` (engine, cards, tests, viewer) + the ledger commit adding this section.

**Cards (cards.json; cost + oracle verbatim from Scryfall, bracket notes appended):** Shadowborn Apostle
(`sac_named_count 6`, `sac_named_activation_cost {B}`, `tutor_types [Creature]`, `tutor_required_subtype Demon`,
`tutor_shuffle_after`), Abhorrent Overlord (`etb_tokens_per_devotion_color B` + 1/1 black Harpy flying spec,
`upkeep_sacrifice_creature 1`), Reaper from the Abyss (`endstep_morbid_destroy_target_creature`,
`endstep_destroy_excluded_subtype Demon`; keywords Flying + Morbid -- `Morbid` added as an inert keyword tag),
Sire of Insanity (`endstep_each_player_discards_hand`), Jetmir (`creature_count_anthem_tiers [3,6,9]`,
`_power 1`, `_keywords [Vigilance, Trample, Double strike]`, Legendary).

**New params:** the eleven above (all default-off). New GameState fields in padding (size 1232 unchanged):
`deck_has_morbid` / `deck_has_count_anthem` / `deck_has_demon_triggers` / `deck_has_sac_named` (deck stamps,
default true when unstamped) and the pin `scripted_demon_trigger`. New Plan field `demon_trigger_choice` (in padding).

**Engine:**
- Apostle: `PermAbilityMode::SacNamedTutorPut` (no {T}). `EmitSacNamedVariants` emits ONE family per board
  (any copy is the source; re-resolved by name in `ResolveHandSourceActivation`). Bodies = a READINESS-COUNT pin
  (`Action::sac_victim_id = 1 + s + 8t`): tapped-and-new copies always first (dominated by every other state),
  then t tapped / u new-untapped / s attack-ready; a code with s > 0 while a weaker copy is left over is NOT
  emitted (sound dominance). T vs U stays searched (INT-1's readiness reasoning). Copies in HAND count as
  arrivals, so "cast two, sacrifice them + four old" is its own code; an unrealised code is a clean no-op.
  Demon = every legal library Demon (Jetmir included) + `(no fetch)` (fail-to-find, still shuffles). Resolver in
  `ApplyPermAbility` (both trailing passes via `SacNamedPinScope`), sacrifices through `SacrificePermanentAt`
  (morbid set), then `PerformTutorToBattlefield` (full ETB cascade). Plan-signature `#N<code>#F<demon>` in the fast
  and legacy builders. One activation per plan; the second is breakpoint site 9: a board-level activation key
  (`kActSacNamed`, carried by the first copy, valued floor(copies/6)) is new exactly when the count crosses a
  multiple of 6 (the sixth arrival, or six left after the plan's own activation). Leaf/rollout keeps the front code.
- Overlord ETB: `DevotionTo` at resolution (own pips count), one `CreateTokens` event.
- `PerformUpkeepSacrificeCreature` (Overlord, after Fading, both worlds), `PerformEndStepMorbidDestroy` (Reaper,
  morbid snapshotted at the top of the end step) and `PerformEndStepDiscardHands` (Sire; also at the opponent's
  notional end step, after their draw) -- all deck-gated, lockstep in GameEngine and SimulateEndAndStartNextTurn.
  Choice lists are identity-folded classes (`DemonTriggerClassReps`: controller + INT-1's base key); Reaper
  orders opponent creatures first, then ours tokens-first (ORDER only).
- Demon-trigger axis (`MTG_DEMON_AXIS`, default on): per base plan with a consumer (Reaper on board / put / cast
  AND a possible death this turn; Overlord on board / put / cast), the CROSS PRODUCT of Reaper ranks (low byte)
  x Overlord ranks (high byte), bounded by the classes now + new names the plan adds + a Harpy class. End step
  consumes the low byte, the next upkeep the rest. Folded in dominance::Build, the dedup pending pins, the
  breakpoint key, plan hash/equality/census. Morbid flag folded in BuildSimKey + dominance (deck_has_morbid).
- Jetmir: count pass in `ComputeLordBonus` (count once per board via `BoardSources::own_creatures`, otherwise
  capped at the top tier), `CountAnthemGrantsKeyword` feeding `CreatureHasDoubleStrike` / `CreatureHasVigilance`;
  Jetmir admitted to bs.anthems / bs.ds / bs.vigilance.
- Second main: `DeckUsesSecondMain` returns true on `sac_named_count > 0` (only this deck carries it).
  `ShadowbornProvider` routing OR gains `sac_named_count > 0`.
- Viewer: line verb `sacnamed=<Demon|(no fetch)>` (LineSpec, CheckLine, plan JSON, index.html); `sacrifice` reuse
  (Apostle's six, with `SacBurstScope` "pick k of 6"; Overlord's upkeep), `target` reuse (Reaper). DECISIONS.md
  rows + audit MANIFEST (`sac_named_count`, `upkeep_sacrifice_creature` -> sacrifice; Reaper -> target).

**Tests:** `test/unit/test_shadowborn_int2.cpp` (10 cases: routing + second main; activation with sick Apostles
and Overlord devotion after the six (2 + 2 left = 4 Harpies); readiness code s / arrivals; two activations and
the order (Overlord first 8 Harpies vs second 5); fail-to-find shuffles, five copies not activatable; Overlord
upkeep pin ranks incl. itself; Reaper own-creature forced target, spawn rank 0 / Apostle rank 1, low-byte-only
consumption; Sire discard keeps staged; Jetmir 0/1/2/3 tiers, vigilance at 3, double strike at 9 = 8 damage vs 3;
demon axis emits Reaper variants). Unit suite 2443/2443, scenarios 398/398.

**Smoke:** 161 passed, 4 failed -- all Merfolk (d0/d3/d5, 2hg d3); merfolk d3 digest 16290806e85e1728 = the
pre-existing drift INT-1 reproduced on HEAD. No other case play-changed. Not accepted.

**Sanity run** (50 games, d3 b10, seed 1001, no profile): avg 6.34, one unwon (gi15, no lands early). Only 2
activations in 50 games (gi18 main 2 after attacking: cast the sixth, Overlord + 2 Harpies, won T6; gi21 main 1
Sire) -- without Thrumming Stone (INT-3) six Apostles are rarely reached. 21 s wall on 20 threads.

**For INT-3 (ripple):** ripple-cast Apostles are NOT counted as arrivals by `EmitSacNamedVariants` (only hand
copies); the activation after a chain is reached through the site-9 key (crossing a multiple of 6, armed post-plan,
once per board -- not per Apostle entry). If a mid-chain activation is wanted, extend the arrival count with the
chain's expected Apostles. Free ripple casts of Intent / Culling must still pay the sacrifice (INT-1 note).

**Left / disclosed gaps:**
- Within one readiness state the copies are taken lowest-number-first: exact while every copy of the name shares
  its base identity (true in this pool); a name split by counters/Auras is a generality gap.
- A third+ trigger ordinal of Reaper/Overlord takes its list front (pins hold 2 ordinals; the axis fans ordinal 0
  only -- 1-ofs here). Multiple Reaper triggers choose targets at resolution, sequentially.
- Demon axis cost: the Overlord half fans every base plan while an Overlord is out (x ~4-6 plans); measure at 2d /
  the matrix; a boundary-fan (echo/DotH shape) is the alternative if it bites.
- Sire's discard passes no madness host (no madness card in the pool).
- `test/check_plan_sig_lockstep.py` fails at HEAD already (`ponder_keep` fast-only) -- not INT-2's.

**New provisional calls (surfaced, USER sign-off owed):**
1. Readiness dominance for the six: tapped-and-new copies first; an attack-ready copy never given up while a
   weaker copy remains (only the counts are searched).
2. Viewer-audit INERT classifications: `tutor_required_subtype`, `sac_named_activation_cost`,
   `endstep_destroy_excluded_subtype`, `endstep_each_player_discards_hand`, `etb_tokens_per_devotion_color`,
   `creature_count_anthem_tiers/_power/_keywords`.
3. Demon-trigger default order (branch order only): Reaper -> opponent creatures first; Overlord -> tokens first.

## INT-3 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `465851b04` (engine, card, tests, viewer) + the ledger commit adding this section.

**Card (cards.json; cost + oracle verbatim from Scryfall, bracket note appended):** Thrumming Stone (`{5}`,
Legendary Artifact, `grants_ripple 4`). **New params:** `grants_ripple` (int; Thrumming Stone) and `ripple` (int;
innate ripple, no card yet). `ShadowbornProvider` routing OR gains `grants_ripple > 0`. **New GameState:**
`deck_has_ripple` (deck stamp) + `ripple_script` (the chain script); 1232 -> 1264 bytes, classified in Dominance.h
(not folded: a chain finishes inside the cast that stamped it). **New Action fields:** `ripple_pinned / _reveals /
_take_cap / _mask / _decline / _free_tutor / _free_victim / _granter_req` (+ CastRiders, both plan signatures `#RP`,
ActionFoldSig, the breakpoint record). **New TriggerKind:** `Ripple`.

**Engine (one set of decisions, three sites, same order):**
- Executor: `EffectHandler::PushCastTriggers` pushes one `Triggered{Ripple}` per instance (`RippleInstanceNs`:
  innate + one per granter; the legend rule leaves at most one Stone) above EVERY cast -- hand casts and every
  `PushFreeCast`, so ripple-cast spells ripple again. `ResolveRippleTrigger` casts all of a reveal's takes FIRST
  (`PushFreeCast` with deferred triggers), then pushes their triggers: stack `[.., S2, S3, R2, R3]`, so one reveal
  casting two tutors reveals both chains before either shuffles (test: a 4th Diabolic Intent is found only in that
  order). `PushFreeCast` now PAYS a readiness-route "sacrifice a creature" additional cost (pinned victim), and a
  free tutor's searched pick rides its stack entry (`RippleTutorRankPrefix`, decoded in `PerformTutor` as the
  scripted-index path).
- Rollout + viewer (`apply_one`): the inline block stamps the hard cast's script and resolves each instance
  through `ripple_trigger` -- cast the takes (PushFreeCast's checks and bookkeeping, same order), resolve their
  chains top of stack first, then resolve the spells through `apply_one` with one-shots (`ripple_precast`: free,
  already counted / paid / triggered; `ripple_cast_number`: THAT copy, not a same-name hand copy; not recorded).
- Upkeep: `CastOffSuspend` now ripples (`ResolveSuspendRipple`, inline, before the draw): Lotus Bloom arrivals
  (reveal y/n is a real choice: rotation before the draw) and Profane Tutor arrivals (F2: take/decline of a
  revealed twin). Pinned on the packed upkeep stream (`scripted_upkeep_land_search`): per ripple-live arrival a
  DECISION ordinal then a FREE-SEARCH ordinal (discarded when unused, so the fan stays aligned); fan (3) in
  `AppendSubdecisionAxes` extended, the fan block opens on a ripple deck for any arrival.
- Decisions (SpellEffects.h "RIPPLE", proofs in the comment): reveal = F1 per-NAME budget for order-inert names
  (`RippleChainOrderInert`: Apostle, Dark Ritual, Culling, Stone, Demons; adjacent copies share it, so n copies cost
  B+1 variants, not (B+1)^n -- continue copies fold), F2 "always reveal" for a trigger directly above a
  searching/shuffling spell under the stable shuffle (equal up to which identical copy the search takes), a
  per-trigger don't-reveal bit otherwise. Take: Apostle-class (`ShadowbornProvider::FreeCastTakeDominated`) cast;
  declines (`take_cap`) only in the LATE wave; Ritual / Stone / Intent / Profane takes are decline bits; free tutor
  picks and free sacrifice victims are rank pins. Playout policy (beyond the horizon only): reveal iff a castable
  same-name card is in the top N, take all, the front pick.
- Planner (`EmitRipplePins`, CollectActions after the victim pass, decision space only): menus sized by a
  library-copy dry run (`RippleChainDryRun`). A Stone only in HAND marks every revealing variant
  `ripple_granter_req` (SubsetHasMissingTrickTarget rejects it without a granter cast in the plan); the
  "no ripple" variant carries no requirement. Dark Ritual variants credit the chain's free Rituals on
  `ritual_float` (approved ahead-of-time credit). `EmitSacNamedVariants` counts the chain's free Apostles as
  arrivals, so the activation is in the SAME plan as the chain (INT-2's gap closed); the site-9 crossing-a-multiple
  -of-six breakpoint still covers the rest (observed: gi15 T5, an 18-reveal chain casting 17 Apostles, two
  activations -- Sire main 1, Overlord main 2 -- win T6).
- LATE WAVE (`RippleLateWaveAdmitted`): a decision whose ROOT budget is unlimited or >= `MTG_RIPPLE_LATE_WAVE_MS`
  (default 100 virtual ms) also gets the Apostle declines and a speculative sub-pin fan for chains the dry run
  cannot see (the plan's earlier casts move the library). Static per decision, so the epoch-scoped memos agree.
- Cast order: `ShadowbornProvider::CastOrderRank` puts the granter first (rank 1); the payable-order fallback
  re-sequences when the Stone needs a ritual first.
- Viewer: `dig` "(ripple reveal)" (spell tile; ON by default, no toggle) and `free_cast` "(ripple)" re-asked after
  each cast ("Cast no more"), asked in the shared `RippleRevealDecision` / `RippleResolveTrigger` (all three sites).
  `WriteDigDecisionJson` note, `digPanelHtml` + `freeCastPanelHtml` branches, DECISIONS.md rows, audit MANIFEST
  `grants_ripple -> dig`; static audit clean.

**Tests:** `test/unit/test_shadowborn_int3.cpp` (10 cases): card data + provider hooks + instances (no Stone /
lands); an Apostle chain casting free Apostles that ripple again, executor == rollout zones, bottom in reveal order,
budget 1 and take-cap 0; Dark Ritual ripple (6 black floating, decline -> 3 and bottomed); no-match reveal bottoms
in reveal order, budget 0 moves nothing, fewer than four cards; Stone legend rule (second Stone ripples, one stays);
free Intent pays its sacrifice + pinned search, uncastable without a creature (bottomed); two tutors from one
reveal (stack order: 4 casts); Lotus Bloom upkeep reveal / don't, Profane Tutor taking / declining its twin; pin
emission (budgets 3..0 on the first copy, continue on the other, granter requirement with the Stone in hand);
the chain's Apostles reach the six (activation emitted; none without the Stone). Unit 2453/2453, scenarios 398/398.

**Smoke** (`test/regression.sh --smoke`, not accepted): 161 passed, 4 failed -- Merfolk d0/d3/d5 + merfolk2hg d3,
the pre-existing drift (merfolk d3 digest 16290806e85e1728, as INT-1/INT-2). Every other case byte-identical,
including Dragonstorm (Lotus Bloom off suspend: ripple inert without a granter).

**Sanity** (50 games, d3 b10, seed 1001, no profile): **avg 5.58** (INT-2: 6.34). **57 activations in 36/50
games** (INT-2: 2). 600 real ripple reveals, 527 free casts. Read game: gi15 T5 one Apostle -> 18 reveals, 17
free Apostles (the chain wraps the library), activation -> Sire main 1, activation -> Overlord (9 Harpies) main 2,
win T6. One game > 30 s at d3 (gi38 2.0M units).

**Branching cost** (per-game work units, MTG_SLOW_GAME_MS=1; "NoStone" = the list with the four Stones as Swamps,
same binary, so ripple is the only difference -- not paired, the shuffle differs):

| cell | list | avg | mean units/game | median | p90 | mean s/game |
|---|---|---|---|---|---|---|
| d3 b10 x50 | shipped | 5.58 | 363,261 | 226,882 | 1,007,436 | 8.3 |
| d3 b10 x50 | NoStone | 6.44 | 128,160 | 50,837 | 324,246 | 2.6 |
| d5 b20 x20 | shipped | 5.60 | 360,241 | 174,526 | 1,061,894 | 5.9 |
| d5 b20 x20 | NoStone | 6.40 | 150,765 | 50,831 | 484,926 | 2.3 |

So ripple costs ~2.4-2.8x units per game. The plan set does NOT explode with identical Apostles: a T4 dump with two
Apostles + a Diabolic Intent in hand and the Stone out enumerates 26 main plans (5 budget variants of the two-Apostle
cast, the rest the existing Intent target/victim fan). The cost is the bigger turns themselves (MTG_RIPPLE_STATS d3:
45.4M search triggers, 35.0M reveals, 33.6M free casts over 50 games). Whether this fits the 3x-Hinata suite rule is
Stage 4-bis's measurement.

**Census (MTG_RIPPLE_STATS=1):** unpinned answers INSIDE the window: d3 x50 = 33,784 (of 45.4M triggers), d5 x20 =
30,490 -- ALL free-tutor picks + free victims of a Diabolic Intent revealed by a chain the enumeration-time dry run
did not foresee (the plan's earlier casts moved the library). They take the front at b10/b20; the late wave fans
them (findable at unlimited / large budget). Every other site 0. Real-game bottom tripwire: d3 1,888 ripple-bottomed
cards, **619 read (drawn or re-revealed) before the next shuffle**; d5 621 / 207.

**New provisional calls (surfaced, USER sign-off owed):**
1. **Bottom order binds more than expected.** The tripwire the USER asked for: ~1/3 of ripple-bottomed cards are
   read again before a shuffle (long Apostle chains wrap the library; turns without an activation draw into the
   block). The approved deferral (reveal order) stands; the name-folded permutation axis or a lazy unordered segment
   is the fix if the USER wants it.
2. Granter cast FIRST in this deck's cast order (`ShadowbornProvider::CastOrderRank` rank 1) -- a cast-order call,
   USER-owned. Reasoning: every ripple is optional, so Stone-first is weakly dominant given payability.
3. Late-wave threshold `MTG_RIPPLE_LATE_WAVE_MS = 100` (a budget-SIZE admission, static per decision): Apostle
   declines and the speculative free-tutor/victim fan. Measurable alternative: the TURN-staged wave the USER prefers.
4. The in-window residual above (free Intent picks/victims on unforeseen chains take the front below the late wave).
   Alternative: fan them at every budget (~x30 on each Intent cast with a Stone out).
5. F1's order-inert list is a blacklist of library-touching params (a new library-touching param must be added).
6. Upkeep pins ride the existing ONE-HOT packed-stream fan (each ordinal varies alone), capped at 5 ordinals; a
   free card cast off a suspend reveal answers its own ripple at rank 0 without an ordinal (unreachable with two
   Profane Tutors).
7. Rollout/executor timing differences that are inert for the pool, disclosed: on-cast triggers of a ripple-cast
   spell fire at its cast in both worlds (match); a spell with both cascade and ripple would order them differently
   (none exists); a precast spell whose target became illegal before it resolves is not modelled as fizzling (no
   targeted spell in the deck).
8. **Unbudgeted cost is high.** A 4-game `--depth 2 --budget-ms 0` probe (seed 1001): one game finished in 265 s
   (3.81M units, win T5); the other three were still running after ~16 min and the probe (an agent experiment) was
   stopped. The late wave is fully open there (Apostle declines + the speculative Intent fan). This bears on the
   suite's unbudgeted backstop rows at Stage 4-bis (a degenerate row fails the tier) -- to be sized there, not here.

## INT-4 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): the INT-4 code commit (provider, engine hook, tests) + the ledger commit adding this section.

**Authority (USER 2026-11-03, verbatim):** *"I recommend we make a heuristic for demon search and for tutoring. Demon
search is easy because you would go Abhorrent Overlord and Jetmir only, preferably in that order, but you need to make
sure you have enough black this or next turn if you want both."* / *"Tutoring is also straightforward. You need 1
Thumming stone, 6 mana and a shadowborn apostle. We should get things in that order for the most part."* / *"The mana
is a little trickier since you might want to get Lake of the dead if you have 2 swamps, dark ritual if you need 2 extra
mana. Culling if you have some extra shadowborns."* / *"We need to actually prune a bunch. Lake and Culling are bad in
multiples, whereas ritual is good."* / *"I would remove the others outright."* (an explicit override of the
completeness / late-wave rule for THESE TWO LISTS ONLY; the INT-3 ripple-decline late wave stays: *"Fair enough on the
search. We'll go with that for now"*) / on the Demon fallback: *"sacrificing none may just be better or the same in
goldfishing"*, *"Because sacrificing 6 to get a 6/6 doesn't actually add power and their abilities aren't useful."*

So both lists are RESTRICTIONS (not orders, not waves): a name left out is offered at no budget, `--budget-ms 0`
included. Everything offered stays searched; the order sets the front (playout default). Roles are read off card
data. `MTG_UNPRUNED` / `UnprunedGate::Tutor` (the viewer's human play, the audit arm) get the generic lists.

**Demon rule** (`ShadowbornProvider::SacNamedPutTargets`, new base hook `DecisionProvider::SacNamedPutTargets`; default
`false` = the engine's own list, byte-identical for every other deck):

| rank | role (card data) | card |
|---|---|---|
| 1 | `etb_tokens_per_devotion_color` (devotion tokens) | Abhorrent Overlord |
| 2 | `creature_count_anthem_tiers` (count anthem) | Jetmir, Nexus of Revels |
| -- | anything else, and the fail-to-find | not offered |

With neither in the library the activation is NOT offered at all (no fallback; USER reasoning: in a goldfish six 1/1
Apostles already make 6 power and can attack now while the Demon arrives sick; Reaper's end-step destroy can only hit
our own creatures, Sire discards our hand). `EmitSacNamedVariants` sets `eval` by provider rank, so the leaf front is
Overlord, Jetmir once Overlord is gone. The unpinned resolution (`PerformTutorToBattlefield`'s TutorCandidates fill)
reads the same list (`TutorCandidates` for `sac_named_count > 0` params). The {B} per activation ("enough black ...
if you want both") is left to the search -- no rule.

**Tutor rule** (`ShadowbornProvider::TutorCandidates`, `tutor_to_hand`: Demonic Tutor, Diabolic Intent, Profane
Tutor -- the cast's searched variants, the ripple free-cast rank pins and Profane Tutor's upkeep pin all index this
one list). The early set, in order; nothing else is offered:

| tier | role (card data) | card | admitted when |
|---|---|---|---|
| 1 | `grants_ripple` | Thrumming Stone | none on our battlefield or in hand (a second is legend-rule dead) |
| 2a | land with `etb_replace_sacrifice_land_subtype` | Lake of the Dead | short of 6 AND no Lake in hand or on our battlefield AND >= 2 lands of that subtype (Swamps) |
| 2b | `ritual_floating_mana` + `sac_additional_creature` | Culling the Weak | short of 6 AND no Culling in hand AND spare Apostles (copies on battlefield + hand > `sac_named_count`) |
| 2c | `ritual_floating_mana`, no additional cost | Dark Ritual | short of 6 (always: good in multiples) |
| 2d | non-land suspend with `sac_for_mana_amount` | Lotus Bloom | short of 6 -- last (PROVISIONAL) |
| 3 | `sac_named_count` creature | Shadowborn Apostle | always |
| -- | Demons, other tutors, basic lands, Blood Crypt, a second Stone, a held Lake / Culling, the mana tier at >= 6 | | not offered |

Precedence inside the mana tier: Lake > Culling > Ritual > Bloom when several apply. FALLBACK (PROVISIONAL): an empty
early set -> the generic list (library order), so a tutor never whiffs for want of a rule.

**"Reachable this or next turn"** (`ShadowbornReachableMana`, documented approximation, no search): lands we control
(1 each) + one land drop per remaining turn of the two (`lands_played_this_turn == 0 ? 2 : 1`), capped by mana lands in
hand (a Lake in hand counts 0) + a Lake crack's surplus (`sac_for_mana_amount - 1`) when a Lake is out with another
land of its victim subtype + every non-land sac-for-mana permanent out (Lotus Bloom) + a suspended one arriving by next
upkeep + each ritual in hand's net gain (floating - mana value; Culling only with a creature available). Short = < 6
(`kManaGoal`: the Stone's 5 + an Apostle's 1, the USER's number).

**Tests:** `test/unit/test_shadowborn_int4.cpp` (6 cases): Demon list = [Overlord, Jetmir], no Reaper / Sire / fail-to-
find, eval fronts Overlord, the unpinned resolution reads the same list, the front resolves to the Overlord; Jetmir
fronts once Overlord is gone; NO activation with neither left, at enumeration and in every main plan of an UNLIMITED
(`EnumerateMainPlansExhaustive`, budget nullptr) enumeration; the tutor tiers (Stone / Lake / Culling / Ritual / Bloom /
Apostle, the precedence, six Apostles = no spare, a held Lake / Culling removes it, one Swamp = no Lake, a held Stone =
no Stone, a held Ritual keeps Ritual); reachable-mana arithmetic (land drops, the Lake surplus, the 6 threshold) and the
generic fallback; Profane Tutor's upkeep pin at ranks -1/0/1/2/9 stays inside the early set (clamps to its last) and
every searched Demonic Tutor variant of a real enumeration indexes the early set; other decks (GenericProvider) keep
the full Demon list + fail-to-find and the full tutor list. Existing tests adjusted: INT-2's activation-mechanics tests
run on a test-local `ShadowbornProvider` with the generic Demon list (they exercise Reaper / Sire / the fail-to-find);
INT-3's "chain Apostles count toward the six" fixture fetches the Overlord instead of the Reaper; INT-1's Profane pin
fixture adds a Stone + an Apostle so the restricted list still has >= 2 names. **Unit 2459/2459.**

**Scenarios 398/398. Smoke** (`test/regression.sh --smoke`, not accepted): 161 passed, 4 failed -- Merfolk d0/d3/d5 +
merfolk2hg d3, the pre-existing drift (merfolk d3 digest 16290806e85e1728, as INT-1/2/3). Every other case
byte-identical (this deck is not yet a suite case).

**Sanity** (50 games, d3 b10, seed 1001, no profile, one `--batch` per binary run concurrently at 11 threads each;
INT-3 HEAD `70109a8c1` built in `.claude/worktrees/sb-int3-base`; its units total equals the INT-3 ledger's sanity
exactly, so INT-3's game logs were reused for the per-card counts):

| | INT-3 HEAD | INT-4 |
|---|---|---|
| avg win turn | 5.58 | 5.60 (8 games faster, 9 slower, 33 same; within noise) |
| batch digest | f186f9a7380b2c15 | afc1fd5324ff23f1 |
| activations | 57 in 36 games | 52 in 39 games |
| Demons put by activations | Jetmir 18, Sire 15, Overlord 14, Reaper 9 | Jetmir 28, Overlord 24 |
| Demonic Tutor fetched | Apostle 5, Stone 3, Reaper 2, Ritual 2, Lake 1 | Stone 3, Lake 2, Apostle 2 |
| Diabolic Intent fetched | Apostle 9, Demonic Tutor 6, Stone 5, Ritual 4, Swamp 3, Intent 3, Culling 2, Jetmir 2, Lake 1, Overlord 1 | Stone 11, Ritual 10, Apostle 7, Lake 4, Culling 4, Swamp 2, Blood Crypt 1 |
| units/game mean / median / p90 / max | 363,261 / 226,882 / 1,007,436 / 1,991,713 | 145,369 / 65,699 / 457,262 / 1,146,902 |

Units/game fall 60% (median 71%). The three land fetches are the tutor FALLBACK: late T5 boards with the Stone out, no
Apostle left in the library and >= 6 mana reachable, so the early set was empty (gi28, gi38 x2) -- 3 of 46 tutor
resolutions. The search picks Jetmir over the front Overlord in a good share of activations (the order is the default,
not a pick). The sanity cannot say how often "neither Demon left" suppressed an activation directly; it is a late-game
state (both 1-ofs already fetched by earlier activations).

**Provisional calls / questions for the USER (surfaced, not blocking):**
1. **Tutor fallback**: when no early-set card is in the library, the full generic list is offered (3 of 46 sanity
   resolutions, all lands). Alternative: offer nothing better than a land (e.g. restrict the fallback to mana), or let
   it whiff-proof with the front only.
2. **Lotus Bloom** sits last in the mana tier (suspend 3 is slow for "this or next turn"); it could be dropped outright.
3. **Reachable-mana definition** (above) and the threshold 6 regardless of whether a Stone is already out (the USER's
   "6 mana" read literally). A Stone already on the battlefield arguably lowers the need to 1 per Apostle + {B} per
   activation.
4. **"Spare Apostles"** = copies on our battlefield + in hand beyond one activation's six (ripple-reachable copies in
   the library are not counted).
5. Demon: no activation when neither Overlord nor Jetmir is left -- implemented as the USER ruled; recorded here
   with the USER's reasoning.

## INT-5 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `036f34907` (engine, provider, card notes, tests) + this ledger commit.
Scope: the Stage 2d adversarial review (`logs/shadowborn/review-2d.md`) and the USER's same-day rulings on the tutor
table, the Overlord victim, the Apostle decline and the fallback.

**Review findings fixed (each with a unit test that fails with the fix reverted -- checked by reverting 1 / 2 / 9 and
by `MTG_OVERLORD_VICTIM_FOLD=0` for 3):**
1. **Free tutor clobbered the hard tutor's pick** (`PerformTutor`). A ripple free tutor now always carries a marker
   (`RippleTutorRankPrefix` + rank, or + `u` = unpinned) and `PerformTutor` saves / restores `g_scripted_tutor_choice`
   around it (the Profane upkeep discipline), so the hard tutor below resolves on the plan's `tutor_choice`.
2. **Executor/rollout ripple divergence with `ripple_mask != 0`.** `PushCastTriggers(state, def, controller, number)`
   stamps the cast copy's number on the Ripple entries (AIEngine hand cast + retrace, `PushFreeCast`,
   `ResolveRippleTrigger`); the executor's "directly above its own spell" test now agrees with the rollout's.
   Lockstep test with masks 0x01 / 0x02 / 0x03.
3. **Overlord victim fold** -- see the proof below.
4. **Free tutor / victim pin cap 2** -> every packed ordinal (`kRipplePinOrdinals` = 4) in a DEEP wave
   (`RippleDeepWaveAdmitted`: unlimited, or a root budget >= `MTG_RIPPLE_DEEP_WAVE_MS`, default 1000 virtual ms);
   2 below it. Budget-staged, reachable at `--budget-ms 0`. The 4-bit width (15 names) stays latent: the
   restricted tutor list is <= 5 names.
5. **Reveal budget silently exhausted.** New `kRippleRevealAll` ("every trigger reveals") replaces B = T at the top
   of the per-name menu at every budget (identical in the dry run's world, still cast-everything when the plan's
   earlier casts lengthen the chain; no extra variant). The late wave adds the finite budgets T .. Tmax-1
   (Tmax = hand + library copies of the name) and decline bits sized by library copies.
6. **Ordinals past the emitted menu.** Mask / decline bytes now carry their emitted menu length in the high nibble
   (`RippleMenuByte` / `RippleMenuCovers`; 0 = hand-built, all 8 bits are pins); an ordinal past the menu is answered
   by the playout policy AND counted (`unpinned_in_window` sites 2 / 4). Chosen over fanning them because the
   ordinals are unforeseeable at enumeration (the dry run cannot see a library the plan moves) and fanning all 8
   bits is a 256x cross product; the census is now honest instead (numbers below).
8. **Late-wave leak through the plan cache.** `FoldPhysicalCards` folds the late / deep wave admissions (ripple
   decks only). Not moot after the USER's Apostle ruling: the late wave still sizes the speculative tutor-chain fan
   and the F1 budgets past the dry run.
9. `ShadowbornReachableMana` reads suspend arrivals on `TurnSeq()`.
7. **Ritual chain over-credit -- NOT fixed, recorded as known waste.** `ritual_float += per * (srch_taken + dom)` is
   the dry run's chain on the pre-plan library; a plan casting one copy, or the payable-order fallback sequencing a
   Ritual before the Stone, over-credits. Both worlds fail the later cast identically (wasted / misdirected search,
   no divergence). A sound fix needs the plan's realised cast order at subset time; not cheap.

**THE OVERLORD VICTIM FOLD (finding 3; USER 2026-11-03: "Abhorrent Overlord can just sacrifice the first Harpy").**
The review's claim ("a Harpy is dominated by EVERY other creature") is false: keeping the Overlord itself means another
forced sacrifice every upkeep, Reaper's forced end-step destroy can hit our creature, Sire discards our hand, and a
DEMON keeper changes which of our creatures Reaper's "non-Demon" destroy must hit. The implemented fold is the provable
part (`SpellEffects.h UpkeepSacFoldReps`, proof in the comment):
- CLAIM: at our upkeep's Overlord trigger, with H a vanilla token (token, no definition, no counters / attachments,
  not animated) and K a CLEAN KEEPER (`CardDefinition::upkeep_fold_keeper`: audited params only, no upkeep-sacrifice /
  end-step destroy / end-step discard, benign keywords; no counters / attachments; no subtype a card we own excludes
  from a destroy target), K at least as ready, power and toughness >= H's, H's keywords within K's + Flying -- and
  EVERY card we own audited (`upkeep_fold_audited`, a raw-JSON key whitelist computed at load, so a new param is
  unaudited until someone audits it) -- sacrificing H is at least as good as sacrificing K.
- PROOF (simulation): world A keeps K, world B keeps H; map every later B decision onto A with K for H. Combat: K's
  damage >= H's (goldfish, no blockers; Flying inert, USER-approved). Every audited reader counts a creature once
  (Jetmir's tiers, Intent / Culling / the next Overlord's fodder, Reaper's target pool -- equal legality by the
  subtype premise). K's extra characteristics are read only in its favour (a NAME count, devotion, card-ness unread).
  K has no drawback. So value(A) >= value(B).
- So: while a Harpy class is present, the Apostle class (and Jetmir when no Reaper is in our pool) leaves the rank
  list; the Overlord itself, Jetmir with Reaper in the pool, Reaper and Sire stay searched. The enumerator's rank bound
  becomes D + max(H, K) (it must still cover an upkeep where the Harpies are gone). The USER's simpler "Harpy first,
  nothing else" would buy NO further units: the bound is set by the no-Harpy case either way. `MTG_OVERLORD_VICTIM_FOLD=0`
  = A/B arm.

**USER tutor table (2026-11-03, final; `ShadowbornProvider::TutorCandidates`, rule table in DecisionProviders.h):**

| tier | no ripple granter on our battlefield | a granter on our battlefield |
|---|---|---|
| 1 | Thrumming Stone, if none on bf or in hand | Shadowborn Apostle, if none in hand |
| 2 | mana, while reachable < 6 | mana, while reachable < 3 |
| 3 | Apostle (first or second: one tier) | Apostle, if one is in hand ("second") |
| 4 | extra mana | extra mana (the 4th mana and beyond) |

- USER quotes: "Once thrumming stone is out we only need 3 or 4 mana maximum"; "2 apostles + 2 activations ... that
  mana is less important than having an apostle in hand"; "The extra Apostle is actually probably better than the
  seventh mana, so we would put them both at 3 and combine 4 and 6"; "Let's change 2 in the Stone on Battlefield case
  to be 3 mana. 4 is the most we need, but not as crucial as having the second apostle."; "We shouldn't search Lotus
  bloom" (Lotus Bloom never offered); spare Apostles = battlefield + hand (confirmed).
- Mana order: Lake of the Dead first when fuelled (>= 2 Swamps, none held); then Dark Ritual > Culling the Weak, EXCEPT
  Culling > Ritual when BOTH (i) an EXPENDABLE sacrifice target is out or castable from hand this turn (after reserving
  one Apostle in hand for the Stone when a Stone is on bf or in hand -- none in hand = nothing expendable -- and one
  creature for an Intent in hand while the Stone is missing, i.e. the Intent earmarked to fetch it; USER-confirmed)
  AND (ii) Culling's net +3 reaches the tier's bar a turn earlier than Ritual's +2 (this-turn reachable mana -- new
  `ShadowbornReachableMana(..., this_turn_only)`: untapped lands, this turn's drop, the pool -- then next turn's).
  USER: "otherwise Dark Ritual which has no drawbacks"; "we should usually get the Thrumming Stone rather than culling
  if you are missing it" (already so: tier 1).
- FALLBACK (USER: "There should always be a tutor fallback or we should not cast the spell at all", "we prune it, not
  return a full list", "Even better if that number is 1"): exactly ONE candidate when every tier is empty -- the first
  present of a basic land producing a colour of the Apostle's cost (Swamp) > a creature of the activation's searched
  subtype it does not fetch and that does not discard our hand (Reaper) > such a creature that does (Sire)
  [order PROVISIONAL]. None -> the list is empty and the tutor is NOT CAST: new hook
  `DecisionProvider::TutorNeedsCandidate` (default false, byte-identical elsewhere) drops the hand cast and makes a
  ripple-revealed copy uncastable (`TutorCastPruned` in `RippleMatchCastable` and the dry run). Human play keeps every
  legal cast (the four Shadowborn scenarios use a Swords filler and need it).
- **Apostle decline PRUNED at every budget** (USER: "We should heuristically prune the decline of Apostles. I can't see
  an advantage."): the INT-3 late-wave `take_cap` variants are gone; the script still honours a hand-built cap.
- **Confirmed, no change** (USER): ripple bottom order = reveal order; cast order Stone-first with the payable-order
  fallback casting rituals before the Stone when it needs their mana ("You just cast them before and go off").

**Tests:** new `test/unit/test_shadowborn_int5.cpp` (13 cases); INT-2 (Overlord ranks under the fold), INT-3
(ExecutorCast passes the number; budget menu tops with ALL; a free Intent's fallback pick), INT-4 (the new table,
the one-candidate fallback) updated. **Unit 2471/2471. Scenarios 398/398. Smoke** (not accepted): 161 passed, 4 failed
-- Merfolk d0/d3/d5 + merfolk2hg d3, the pre-existing drift (merfolk d3 digest 16290806e85e1728); every other case
byte-identical.

**Sanity** (d3 b10, seed 1001, 50 games, `--batch`, 8 threads per arm, run together; NOTE the deck folder now holds the
Stage 4 `Shadowborn Apostle.profile.json`, which the batch picks up, so all three arms ran with it -- the INT-4 arm
re-measures at 7,283,597 units / digest aae1fc798c6bfb44 rather than the ledger's profile-less 7,268,498):

| | INT-4 `ae8cbd483` | INT-5 | INT-5, fold off |
|---|---|---|---|
| avg win turn | 5.60 | **5.42** | 5.44 |
| units total (per game) | 7,283,597 (145,672) | **4,742,588 (94,852)** | 5,943,151 (118,863) |
| batch digest | aae1fc798c6bfb44 | cbe610e63edc2f7f | 234a277fd63e2c0d |
| activations | 52 in 39 games | 51 in 40 | 51 in 40 |
| Demons put/entered | Jetmir 28, Overlord 25, Reaper 5, Sire 1 | Jetmir 30, Overlord 22, Reaper 5, Sire 1 | same as INT-5 |
| Diabolic Intent fetched | Stone 11, Ritual 10, Apostle 7, Lake 4, Culling 4, Swamp 2, Blood Crypt 1 | Ritual 12, Apostle 9, Stone 9, Lake 6 | Ritual 13, Apostle 10, Stone 9, Lake 6 |

The fold alone: -20.2% units (5.94M -> 4.74M) at equal-within-noise play (5.44 vs 5.42). INT-5 vs INT-4 overall:
-34.9% units and -0.18 turns (the tutor table and the fixes together; 50 games, not a held-out A/B).

**Census (MTG_RIPPLE_STATS, same runs), now honest:** INT-5 unpinned_in_window = 5,280 of 16.8M triggers: tutor 1,535 +
victim 1,535 (free Intents on chains the dry run could not foresee, below the late wave), **take/past-bits 2,110 and
reveal/past-mask 100 -- the residual finding 6 showed was previously read silently as "take / reveal"** (INT-4's census
showed 0 there). Real-game bottom tripwire: 2,053 ripple-bottomed, 648 read before a shuffle.

**Open / provisional (surfaced, not blocking):**
1. Fallback order basic land > Reaper > Sire (the coordinator's PROVISIONAL order).
2. The Overlord fold keeps the Overlord-self / Demon-with-Reaper / Sire options searched (sound), rather than the USER's
   literal "Harpy only"; same units either way (above).
3. The deep wave (`MTG_RIPPLE_DEEP_WAVE_MS` = 1000) multiplies the unbudgeted late-wave cross product by ~wt*wv per extra
   ordinal; INT-3's note 8 (unbudgeted cost) gets worse -- to be sized with the unbudgeted backstop rows at Stage 4-bis.
   The lasting fix is to decide free-tutor picks at their resolution (a breakpoint) instead of a packed cross product.
4. Finding 7 (ritual chain over-credit) left as known waste.
5. The residual in-window unpinned answers (5,280 at d3 b10) take the playout policy below the late wave, by design.

## INT-6 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `9bb67b540` (provider, heurarm slot, tests) + this ledger commit.
Scope: analyze-deck Stage 5i, the USER-designed cleanup-discard buckets (brief: `logs/shadowborn/int6-discard-brief.md`,
the REVISED fill order). Summaries: `logs/shadowborn/int6/`.

**Authority (USER 2026-11-03, verbatim):** *"We should bucket as usual, 1 Thumming Stone (or tutor if missing) between
hand and battlefield, mana up to 6, 1-2 shadowborn. Tutors fill spots where something is missing including mana, though
we need to keep a space for Shadowborns to be sacrificed to Diabolic Intent and Culling. 1 Shadowborn at least should go
before 6 slow mana. I don't want to see it keep Thumming Stone and 6 lands."* / *"Fast mana like Dark Ritual is pretty
much always a keep. Same with culling and lake of the dead in situations you mentioned. Naturally, we need to have fodder
for the things that require it and at least a couple of land between hand and battlefield."* / *"Maybe with stone out,
you would want to keep all Shadowborns and maybe even Reaper as a side threat to consider hardcasting."*

**The rule** (`ShadowbornProvider::CleanupDiscardShedOrder`; roles from card data; keep set quota-first, NET OF BOARD;
the shed is the overflow's first card, else the last card kept; ONE index through `CleanupDiscardRankingWithOrder`;
re-asked per shed, `CleanupDiscardShedStable() == false`):

| # | bucket | keeps |
|---|---|---|
| 1 | STONE | 1 ripple granter (`grants_ripple`) between hand + battlefield; with none in either, 1 TUTOR holds the slot (Demonic > Intent with fodder > Profane > Intent) |
| 2 | LANDS | lands until 2 between hand + battlefield (a Lake in hand does not count; basics first) |
| 3 | APOSTLE #1 | 1 `sac_named_count` creature; none in hand -> a tutor fills it (PROVISIONAL) |
| 4 | FAST MANA + FODDER | NOT capped: every plain ritual (Dark Ritual); the first Lake when fuelled (>= 2 lands of its subtype on our battlefield, no Lake there); the first creature-sac ritual (Culling) when it has fodder. FODDER: each kept creature-sac card (Culling, a kept Diabolic Intent) claims one of our battlefield creatures, else keeps one more hand Apostle (never #1; a slot-1 Intent's claim is deferred until #1 is kept) |
| 5 | MANA to quota | 6, or **4** with a granter ON OUR BATTLEFIELD; counted = `ShadowbornReachableMana(board_only)` + kept hand cards (land 1, ritual its net, Lake its crack surplus 3, a tutor 1); fills with lands, then a tutor |
| 6 | APOSTLE #2 | no granter out: one more. Granter out: EVERY Apostle, then the first HARDCAST THREAT (a creature of the Apostle's searched subtype the activation does not fetch, no hand discard: Reaper) |
| 7 | OVERFLOW (shed first) | unclassified; a granter in hand while one is out; Demons (colour-unreachable, then the hand-discarder, then higher MV); a second granter; Lotus Bloom; Lake / Culling outside their conditions; surplus lands; extra tutors; extra Apostles; rituals |

Lever: `MTG_SHADOWBORN_BUCKET_DISCARD` (per-job heurarm slot, DEFAULT ON pending the USER; =0 -> `DeckProvider`'s generic
max-MV ranking). `MTG_SHADOWBORN_DISCARD_FAN=1` = TESTING-ONLY fan for the 5i evidence run. `ShadowbornReachableMana`
gains a `board_only` mode (existing callers unchanged).

**Tests:** `test/unit/test_shadowborn_int6.cpp` (10 cases): lever off = generic (sheds the Stone, max MV) and
prefix-stable; on = one index, re-asked; Stone + 6 lands + Apostle -> a land (and with 3 lands out, three surplus Swamps
before the Apostle); a granter in hand with one out sheds FIRST (ahead of Jetmir / Overlord), and is the last card kept
without one out; a second granter after the Demons, before Lotus Bloom; every Dark Ritual kept past the quota; Culling
kept with its fodder Apostle (and with a battlefield creature), overflow with no fodder; a slot-1 Intent keeps a fodder
Apostle ahead of the mana, Demonic Tutor preferred for the slot; Lake fuelled / unfuelled / Lake already out; granter
out -> quota 4, every Apostle, Reaper over surplus lands (and Reaper shed first with no Stone out); the Demon order.
**Unit 2481/2481. Scenarios 398/398. Smoke** (not accepted): 161 passed, 4 failed -- Merfolk d0/d3/d5 + merfolk2hg d3,
the pre-existing drift (merfolk d3 digest 16290806e85e1728); every other case byte-identical (deck not a suite case yet).

**Where this deck discards** (`MTG_TRACE=discard`, `MTG_SHED_STATS`): the cleanup is the only CHOICE site (Sire's
end-step discard is the whole hand; no other card discards). REAL sheds are rare: 1 per 50 games (INT-5 / lever off),
2 per 50 (lever on) at d3 b10; 5-6 labelled per 400 games in the evidence runs; hand size 8 every time. ROLLOUT sheds
are ~31 per game (1,546 per 50 games, all "low-land" < 4 lands out), and the bucket pick differs from the generic index
0 in **67.4%** of them -- so the rule's effect is almost entirely search/rollout fidelity, not real sheds.

**5i evidence** (`analyze_deck.py --discard-analysis --no-rebuild`, 400 games d3 b10 single-thread, seed 930000):

| arm | labelled decisions | rule picks optimal | mean regret | verdict |
|---|---|---|---|---|
| generic (lever =0, the stage's status quo) | 5 | 5/5 | 0.00 | STATUS_QUO_OK (no derived order, no A/B) |
| bucket rule (`MTG_SHADOWBORN_DISCARD_FAN=1`) | 6 | **6/6** | **0.00** | STATUS_QUO_OK |

Rule-vs-searched regret is ZERO. The bucket picks were: a second Stone (no Stone out), Overlord (Demon overflow) x2,
a third Apostle (Stone + 4 lands + 2 Apostles kept), Reaper (no Stone out), Jetmir (colour-unreachable first). The
alternatives the search refutes are exactly what the rule keeps: shedding the only Stone (win 9 vs 7 at seed 930223),
Culling, Dark Ritual, the needed lands. All 6 decisions are multi-optimal (low stakes), and 6 labels is a thin sample.

**Sanity** (d3 b10, seed 1001, 50 games, `--batch`, the deck's untracked profile picked up; INT-5 binary
`build/scratch/shadowborn/int5/mtg.int5` = 684240425's src at 8 threads, the INT-6 binary's on + off arms as ONE pooled
batch at 16 threads, run together):

| | INT-5 `684240425` | INT-6, buckets ON | INT-6, `=0` |
|---|---|---|---|
| avg win turn | 5.42 | 5.42 | 5.42 |
| batch digest | cbe610e63edc2f7f | be7da10f87c3ab2f | **cbe610e63edc2f7f** (byte-identical to INT-5) |
| units total | 4,742,588 | 4,698,192 (-0.94%) | 4,742,588 |
| per game vs `=0` | | 47 identical, 1 faster (gi32 6->5), 1 slower (gi2 5->6), 1 same-turn play change | |

Both moved games turn on the same line: the search PASSES TURN 1 (no land drop with lands in hand, nothing cast) and sheds
at cleanup -- gi2 does it under the buckets, gi32 under the generic rule; the other arm plays Swamp + Apostle. So the
moves are the rollout policy tipping a near-tie, not a bad shed (gi2's shed was Jetmir, the right card).

**FINDING (outside INT-6; surfaced, not fixed): the search sometimes passes turn 1 without a land drop.** Every labelled
real shed in the evidence runs is a turn-1 (one turn-2) cleanup at hand size 8, i.e. a T1 on the draw with no land
played: ~1.5% of games show it as a shed, and on the play the same pass would leave no shed to see. Declining a free land
drop plus a castable 1-drop is near-dominated; likely a horizon tie the null plan wins at d3 b10. Owed: a single-game
repro at `--depth 8 --budget-ms 0` (e.g. `--seed 1003 --game-index 2 --games 1`, buckets on) to see whether depth
recovers it, then root-cause.

**Provisional calls / questions for the USER (surfaced, not blocking):**
1. **Adopt?** Default ON on this branch (non-inferior at the sanity: same avg, -0.94% units, zero label regret). The
   lever-off arm is byte-identical to INT-5. A held-out A/B sized to the effect (rollout fidelity) is the adoption test
   once the deck is in the suite.
2. **Tutor fills a missing Apostle #1** (the USER's general "tutors fill spots where something is missing"; not in the
   REVISED list). A tutor kept for a mana slot counts as **1** mana.
3. **Fodder** = any creature we control on the battlefield (the INT-5 "expendable" reading), else a hand Apostle beyond #1.
   A fodderless Intent still holds the Stone slot when it is the only tutor; it is not kept for a mana slot.
4. **Lake counts +3** (its crack surplus) toward the quota when kept; "fuelled" = >= 2 Swamps on our battlefield (hand
   Swamps do not count: the Lake and a Swamp compete for the same land drop).
5. **Demon shed order** inside the overflow: colour-unreachable (Jetmir) > hand-discarder (Sire) > higher MV (Overlord
   before Reaper). Reaper is kept (step 6) only with a granter out, after every Apostle and the 4-mana quota, ahead of
   surplus lands (the brief's provisional placement).
6. "Dark Ritual last of all" in the overflow list is inert: rituals are always kept, so among KEPT cards the shed order
   is reverse fill order (a Ritual outlasts the quota lands but not the two-land floor, the Stone or Apostle #1).

## INT-7 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `969bfc8ab` (hooks, provider, tests) + this ledger commit.

**Authority (USER 2026-11-03, verbatim):** *"Abhorrent Overlord can just sacrifice the first Harpy."* / *"The Overlord and
Sire attack for 5 and 6. They hit hard enough to be worth keeping over a harpy."* / *"I don't think we should fully search
that. Maybe choose a Shadowborn next and finally the Overlord?"* / *"It's worth testing out, anyway. This seems better than
wasting search time."*

**The rule** (`ShadowbornProvider::UpkeepSacrificeVictim`, through the new `DecisionProvider::UpkeepSacrificeVictim` /
`UpkeepSacrificeVictimFixed` hooks; default false, so every other deck is byte-identical). The Overlord's upkeep sacrifice
is NEVER searched: exactly ONE victim, roles off card data --
1. a **Harpy**: a token creature (no CardDefinition) matching a devotion-token maker's `etb_created_token_*` spec;
2. else a **`sac_named_count` creature** (Shadowborn Apostle);
   within 1-2 the weakest copy (tapped, then summoning-sick, then the first -- equivalent at the upkeep);
3. else the trigger's **own source** (the Overlord itself; `PerformUpkeepSacrificeCreature` now records each trigger's
   source);
4. else (the source already gone, e.g. a second Overlord's trigger ate it) the generic front of `UpkeepSacFoldReps`.
Sire, Reaper, Jetmir and another Overlord are never offered while any of 1-3 exists. A USER restriction at every budget
(`--budget-ms 0` included); the demon-trigger axis emits no Overlord victim variants (Reaper's target axis unchanged).
Global `MTG_UNPRUNED` (the audit arm) declines the hook and restores the INT-5 searched fold. Human play keeps the full
`sacrifice` click (this pick is its default). The INT-5 proof comment stays at `SpellEffects.h`, marked SUPERSEDED for
this deck by the USER rule. `cards.json` Overlord bracket note updated.

**Tests:** INT-5's fold tests now run the searched shape through a test subclass (`SearchedVictimProv5`) and also assert
the shipped shape always takes the Harpy; new INT-7 cases: Harpy + Sire + Overlord -> the Harpy only, at every pin;
no Harpy -> the (tapped) Apostle, a non-spec 2/2 token ignored, Sire / Jetmir / Overlord kept; no Harpy / Apostle -> the
Overlord itself; two Overlords + one Harpy -> Harpy then the second trigger's own source; source gone -> the generic
front; no enumerator victim variants. INT-2's Overlord test updated (a pin no longer moves the victim off the Harpy).
**Unit 2485/2485. Scenarios 398/398. Smoke** (not accepted): 161 passed, 4 failed -- the same pre-existing Merfolk
d0/d3/d5 + merfolk2hg d3 digests as INT-6; every other case byte-identical (deck not a suite case yet).

**Measurement vs INT-6 `aa230c463`** (binary `build/scratch/shadowborn/int6/mtg.int6`, its own cards.json; INT-7 binary
of `969bfc8ab`; Release, `MTG_PGO=0`; one pooled `--batch` per binary at 12 threads each, run together; the deck's
untracked profile; summaries under `build/scratch/shadowborn/int7/ab/`). Avg = the batch's (non-wins penalised); both
arms have the same non-wins (gi103 at d3; gi103, gi125 at d5):

| cell | games | avg INT-6 | avg INT-7 | faster / slower (paired) | units/game INT-6 | units/game INT-7 |
|---|---|---|---|---|---|---|
| d3 b10, seed 1001 (sanity) | 50 | 5.420 | 5.420 | 0 / 0 (49 identical digests) | 93,977 | 69,007 (**-26.6%**) |
| d3 b10, seed 7001 (held-out) | 200 | 5.455 | 5.435 | **3 / 0** (gi5 7->5, gi106 6->5, gi151 6->5) | 161,820 | 121,677 (**-24.8%**) |
| d5 b20, seed 7001 (held-out) | 200 | 5.405 | 5.385 | **4 / 0** (gi29, gi46, gi82, gi152 6->5) | 271,482 | 192,934 (**-28.9%**) |

No slower game in any cell, so the d5 b200 single-game recovery check had nothing to run. INT-7 is strictly better on
the held-out sample at both cells and cheaper by a quarter of the units: the budget the searched victim axis was eating
now goes to the rest of the turn.

**Open (surfaced, not blocking):** the weakest-copy tie order (tapped > sick > first) is PROVISIONAL and inert at the
upkeep; with no Harpy and no Apostle the Overlord sacrifices ITSELF ahead of Sire / Jetmir / Reaper, per the USER's order.
**Both CONFIRMED by the USER at INT-9** (see INT-9's ruling 3).

## INT-8 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `b15fe7f37` (provider, engine, tests, design doc) + this ledger commit.
Brief: `logs/shadowborn/int8-brief.md`. Scratch: `build/scratch/shadowborn/int8/`.

**A. Deleting the ripple late / deep waves: MEASURED TOO WIDE, NOT SHIPPED (USER decision owed).** Both waves were
deleted as asked, so every speculative menu and every packed pick ordinal was emitted at every budget. Measured result:
- One probe state (Stone out, a hard Diabolic Intent, three Intents in the library): **5,488 variants of that single
  cast**. The cross product is 8 don't-reveal masks x 8 decline masks x 3^k free-tutor ranks x 2^k victim ranks, with
  k up to 3. For comparison: 2,608 / 2,744 with the deep or the late part alone, and 1,304 in the dry-run-only shape.
- Real play: in the d5 b20 held-out cell, single games ran **past 20 minutes**. INT-7 plays the whole 200-game cell in
  ~2,000 game-seconds. The A/B arm was stopped. Most of that cost is uncharged enumeration and per-variant applies.
So the waves are back exactly as at INT-7. The brief's sanctioned fix is to decide the free picks at RESOLUTION
instead of pre-building the cross product. That needs a mid-chain choice point, i.e. a resumable ripple stack in
`GameState` so the rollout can branch mid-resolution. It is a lockstep engine change across the executor, the rollout
and the viewer, written up as deferred work in `docs/design/shadowborn-ripple-resolution-picks.md`. It is NOT a new
threshold.

**B. Discard amendments.**
- **4a** (USER: *"a tutor can hold the Apostle slot if it is Profane or Demonic Tutor, not Diabolic Intent, since the
  intent kills an Apostle"*): Apostle #1 accepts only a tutor with no creature-sacrifice cost (Demonic > Profane).
- **4c** (USER: *"discount its value because a lake that has sufficient fuel next turn can be played the turn after for
  the full 6 mana"*): Swamps in HAND now fuel a Lake.
  - In practice this goes through E's route: a Lake fuelled from hand is valued by the turn it can crack, which is
    exactly the discount the USER describes.
  - In the no-route fallback, a Lake with battlefield + hand >= 2 Swamps is kept in step 5 as a land (+1), after a
    battlefield-fuelled one.
  - That fallback sub-branch cannot fire at the shipped quotas: any fuelled Lake reaches 4 / 6 inside the horizon.

**C. USER confirmations recorded (no code change).**
- 4d Demon shed order CONFIRMED (USER: *"Jetmir sounds like the first discard and Sire also makes sense, particularly
  because it also requires Red. Overlord vs Reaper is arguable but I'm okay with your idea. It makes sense to keep the
  cheaper creature even if sometimes the 7-mana one can be dropped after we go off."*).
- 4b fodder, 4e (Ritual always kept) and 4f (buckets on by default, pending the held-out A/B) stand as INT-6 wrote
  them. 4e is now qualified by E: an OFF-route Ritual is pitchable, last of the overflow.

**D. Finding 7 fixed (ritual chain credit).** The chain's free-Ritual mana is split by copy:
- The FIRST copy's variant (B, m) carries c_1(B, m), its own chain.
- Each CONTINUE copy carries inc = max over the menu and copies of c_i - c_(i-1). So c_1 + j*inc >= c_(j+1): still an
  UPPER bound for every order-free payability bound, which therefore stays sound.
- The chain share also rides the new `Action::ripple_chain_float`. ManaPayment's ORDER walks (`FirstUnpayablePos`, the
  exact walk) credit it only once a ripple granter is on our battlefield or was cast EARLIER in the walked order. A
  Ritual sequenced before the Stone ripples nothing, so the payable-order rescue no longer re-sequences onto phantom
  mana. That part is exact.
- What remains loose:
  - a multi-copy plan whose (B, m) increments are below inc;
  - a continue copy cast without the first (its budget reads 0, but it is credited inc);
  - the dry run's pre-plan library (as before).

**E. EARLIEST TURN TO THE BAR** (`ShadowbornProvider::ShadowbornEarliestTurnToBar`; USER: *"look at when we can hit 6
mana instead ... So essentially you look at each card in terms of when it can be deployed. Same deal with Lotus
Bloom."*). The full definition is in the declaration. In short:
- The earliest turn (0 = this turn) at which one turn's mana reaches the bar, simulated with one land drop per turn
  (this turn's drop only if unused; none this turn when called from the cleanup). It counts:
  - lands;
  - the Lake's entry (sacrifices a Swamp, tapped first) and crack (+3 per remaining Swamp);
  - the floating pool (this turn only);
  - Lotus Bloom on the battlefield, already suspended (arrive_turn - TurnSeq()), or IN HAND (suspended now, arriving
    suspend_time_counters turns later);
  - each ritual's net once the turn has a mana;
  - Culling only with fodder.
- The ROUTE is the minimal set of hand mana cards that keeps the same earliest turn. Cards are dropped least-wanted
  first: suspend mana, Culling, Lakes, other lands, non-basic Swamps, basic Swamps, Rituals.
- No route inside the 6-turn horizon -> every mana card is on it.
- **Discard precedence (chosen):**
  - When a route exists, steps 4-5 keep EXACTLY its mana cards: step 4 its rituals / Lakes / Culling (with fodder) /
    suspend mana, step 5 its lands (net of the two-land floor). The quota is treated as met.
  - Off-route mana is overflow in its usual slot (Lake / Bloom early, surplus lands, an off-route Ritual last of all).
  - No route -> the INT-6 rules.
- **Tutor tiers (PROVISIONAL reading, surfaced):** "short" = the earliest turn to the bar (6, or 3 with a granter out)
  is LATER than next turn. Mana candidates are ranked by the earliest turn with the candidate in hand, soonest first.
  Lake > Dark Ritual > Culling breaks ties.
- **Culling vs Ritual:** Culling counts its +3 only with the expendable target (a), so it leads only when (a) holds
  and it reaches the bar a turn earlier: the INT-5 rule, now read off the same helper.

**Tests:** new `test/unit/test_shadowborn_int8.cpp` (11 cases), covering:
- the USER's two examples verbatim: the helper (turn 2 with the Lake on the route; turn 1 with the Lake off it) AND
  at the cleanup (the Lake is kept / the Lake is the first shed);
- Lotus Bloom by its deploy turn (turn 3 on the route; off it once a Ritual + Swamp reach turn 1; a suspended Bloom
  arriving next upkeep);
- a granter-out bar of 3 (tapped lands, start_next_turn);
- Bloom kept / pitched in the buckets;
- 4a (Intent not kept for the slot, Demonic / Profane kept, Demonic over Profane);
- the tutor tier order (Ritual ahead of a fuelled Lake when it is a turn sooner; a tie keeps Lake first);
- the chain credit split (first copy 0, continue copy 3);
- the order walk (no rescue onto the phantom chain; the rescue does use the chain once the Stone is out).

Existing tests updated where the better model changes the answer:
- INT-4 / INT-5: a held Lake cracking this turn means not short.
- INT-6: a Bloom on the only route is kept; a Lake fuelled by hand Swamps is kept.

**Unit 2496/2496. Scenarios 398/398. Smoke** (not accepted): 161 passed, 4 failed. The failures are the pre-existing
Merfolk d0/d3/d5 + merfolk2hg d3 (merfolk d3 digest 16290806e85e1728). Every other case is byte-identical.

**Measurement vs INT-7** (`mtg.int7` = 969bfc8ab src = HEAD 8a5d2b884 src; Release, `MTG_PGO=0`; the deck's untracked
profile; pooled `--batch` per binary; INT-7 at 12 threads, the INT-8 arm re-run alone at 24 after the wave revert;
units are deterministic). Avg = the batch's (non-wins penalised):

| cell | games | avg INT-7 | avg INT-8 | faster / slower (paired) | identical digests | units/game INT-7 | units/game INT-8 |
|---|---|---|---|---|---|---|---|
| d3 b10, seed 1001 (sanity) | 50 | 5.420 | 5.420 | 0 / 0 | 47 | 69,007 | 69,883 (+1.3%) |
| d3 b10, seed 7001 (held-out) | 200 | 5.435 | 5.445 | 1 / 3 (gi132 5->4; gi8, gi28, gi159 5->6) | 181 | 121,677 | 119,059 (-2.2%) |
| d5 b20, seed 7001 (held-out) | 200 | 5.385 | 5.390 | 1 / 2 (gi132 5->4; gi8, gi159 5->6) | 184 | 192,934 | 188,887 (-2.1%) |

- **Plan counts** (MTG_TURN_CENSUS, sanity cell, single-threaded, uncontended): cand_scored 2,180,683 -> 2,198,778
  (+0.8%), enum_emitted 1,993,342 -> 2,010,651 (+0.9%).
- **Slower d5 games replayed at d5 b200: both recover.**
  - gi8: INT-8 5 = INT-7 5.
  - gi159: 4 = 4.
  - gi28 (d3-only): 5 = 5.
- **gi8's divergence:** on T1 INT-8 holds the Apostle (Swamp only) and casts Demonic Tutor on T2. INT-7 casts the
  Apostle on T1. It is a near-tie that budget resolves, the same T1-pass family INT-6 recorded.
- **Net:** +2 / +1 turn-quanta on 200 held-out games (worse by one or two games), 2% fewer units. NOT a strict
  improvement on this sample, and too small to read as a regression.
- **Budget-0 d3 singles** (seed 1001 gi0-3, one thread each, both binaries):
  - gi3: 2.2 s / 94,785 units.
  - gi2: 72 s / 3.45M units.
  - Both identical across arms (same digests).
  - **gi0 and gi1 were still running after 10 minutes in BOTH arms and were stopped.** Unbudgeted cost is unchanged by
    INT-8 (the waves are back) and stays degenerate for half the probe, as INT-3 note 8 reported. This is for the
    unbudgeted backstop rows at Stage 4-bis.

**Decisions for the USER (surfaced, not blocking):**
1. **A, the waves.** Deleting them is unplayable (numbers above). They stay at INT-7's thresholds until the
   resolution-time picks land (`docs/design/shadowborn-ripple-resolution-picks.md`). Build that next, or accept the
   waves meanwhile?
2. **E's tutor reading** (CONFIRMED by the USER at INT-9): "mana while the earliest turn is later than next turn" (provisional), and ranking mana
   candidates by their own earliest turn (this can put Dark Ritual ahead of a fuelled Lake when the Ritual is a turn
   sooner).
3. **E's discard precedence** (CONFIRMED by the USER at INT-9): route cards replace the quota fill; an off-route Ritual is pitchable, but last of the
   overflow.
4. **The held-out result is +1 / +2 turn-quanta worse on 200 games at -2% units.** Every slower game recovers at
   d5 b200. A larger held-out A/B can size it if you want adoption evidence.

## INT-9 done (2026-11-03)
Commits on `shadowborn-analysis` (not pushed): `853849aff` (site census), `28c053fe9` (WIP: every INT-9 prune),
`a73269590` (tests + Lake land signature), `1a430750d` (waves deleted), `95a26cdd5` + `3260bf5af` (Demon timing fixes
found by the A/B), `83d04394c` (comment), + this ledger commit. Brief `logs/shadowborn/int9-brief.md`, hand-off
`logs/shadowborn/int9-handoff.md`. Scratch `build/scratch/shadowborn/int9/`; summaries `logs/shadowborn/int9/`.

**USER rulings, all CONFIRMED (verbatim):**
1. INT-8 E's tutor reading ("short" = the earliest turn to the bar is later than next turn; mana candidates ranked by
   their own earliest turn), INT-8 E's discard precedence (route replaces quota fill; an off-route Ritual pitchable but
   shed last), and brief A.4's victim order.
2. Free Diabolic Intent: *"4 seems potentially okay, but honestly I'm considering whether we should just never cast it.
   Sacrificing an extra spare creature when we already have a search happening seems kind of poor."* -> DEFAULT never
   take; the take-iff-expendable arm (`MTG_SHADOWBORN_FREE_INTENT_TAKE=1`) measured below. Hard Intent unaffected.
3. INT-7's open items: *"The Apostle rule is also good."* and *"Didn't I approve the Overlord sacrificing itself first
   after the small critters are gone already? I actually suggested that. ... Realistically it will never happen in a
   real game that the Abhorrent is a win and Jetmir not."* -> INT-7's weakest-copy order and the Overlord-itself step
   are CONFIRMED.
4. One sacrifice order: *"The sacrifice for other effects (than Overlord) can use the same order. We prioritize
   Shadowborns over Harpies and other demons over Overlord."* + *"Sire, Reaper, Jetmir in that order would be fine if we
   want a full list. Jetmir is essentially the only one without a drawback in this deck."* -> Harpy > Apostle > Overlord
   > Sire > Reaper > Jetmir for every creature sacrifice (incl. INT-7 step 4). Apostle activation still eats Apostles
   only; Lake still a basic Swamp before Blood Crypt.
5. E-bis amendment: *"If we have only 7 or so shadowborns on the field ... Abhorrent overlord (second main) is the best
   option there. ... Jetmir is not that good in the situation where we lose almost all of the creatures."* -> Jetmir
   only when it raises this turn's damage (lethal first), else Overlord main 2; then *"That rule sounds reasonable to
   me."*
6. P1: *"That makes sense. We don't have a good reason to hold lands."* + *"(beyond the Lake of the Dead case you
   mentioned. That is held for the burst turn)"* -> only no-land plans dropped; Swamp + Lake in hand keeps both land
   plans (test added).
7. Sick turn: *"On a summoning sick turn you can do demon search main 1 if you want."* -> sick-turn activations in
   main 1 only (was both mains).
8. Reveal: *"Always revealing is probably okay, arguably not correct only in 1v1. We could always make that an option
   and just fully prune it with heuristics if we want."* -> already that shape: the engine offers the reveal ("may";
   searched masks under `MTG_UNPRUNED`, the dig chooser in the viewer) and `ShadowbornProvider::RippleFixed` prunes it
   to always-reveal. No code change.
9. *"The existing lethal check makes sense."* -> `UseLethalShortCircuit` opted in for this deck, CONFIRMED.
10. Process: a subagent past ~250K context hands off (INT-9 did once).

**A. Inventory** (menu = mean / max width per call, sanity cell d3 b10 + d5 b20, seed 1001, 100 games; census tool =
the per-site `[sbsite]` census under `MTG_RIPPLE_STATS=1`, commit `853849aff`):

| site | before (INT-8) | already pruned by | INT-9 prune | after |
|---|---|---|---|---|
| Demonic / Profane / Intent search, hard (`tutor_axis`) | 2.33 / 4 | USER tables (INT-4/5/8) | kept (cheap) | 3.06 / 4 |
| free (ripple) tutor pick (`tutor_cast.pick`) | 2.49 / 4 | -- | front of the USER table at resolution | 1 / 1 |
| Apostle activation: six bodies + Demon (`sac_named`) | 1.81 / 14 | INT-4 Demon list | weakest six (one code); Demon x phase rule | 1.09 / 2 |
| ripple casts, Apostle (`f1_apostle`) | 9.97 / 19 | INT-5 Apostle decline | one variant (+ granter-free twin) | 1.84 / 2 |
| ripple casts, Ritual / other (`f1_ritual`, `f1_other`) | 2.65 / 33, 2.45 / 33 | -- | take every free Ritual / Stone / Profane; reveal all | 1.82 / 2, 1.99 / 2 |
| tutor-family ripple cast (`tutor_cast`) | 3.50 / 1,444 | waves | reveal all, free Intent declined, front pick, live victim | 1 / 1 |
| "sacrifice a creature" cost (`sac_victim`) | 2.72 / 6 | -- | ONE victim, the deck's sacrifice order | 0.98 / 1 |
| Overlord upkeep sacrifice | 1 | INT-7 | (order CONFIRMED) | 1 |
| Lake ETB keep-vs-graveyard | 1 | not a branch (`ChooseSubtypeLandSacrifice`) | -- | 1 |
| legend rule (free Stone) | 1 | keeps the oldest copy | -- | 1 |
| land / no-land | -- | -- | P1: no no-land plan while a free land is in hand | -- |
| one-activation verified kill | -- | -- | the frame's only plan (+ the land family's idle baseline) | -- |

Ripple triggers in the sanity cell: 24.6M -> 1.64M; unpinned-in-window answers 10,029 -> 0. Files:
`logs/shadowborn/int9/census_before.txt`, `census_after.txt`. The deck has a full searched second main.

**F.1 factor table.** INT-8's 5,488 variants of one hard Intent cast (Stone out, 3 Intents in the library) = 2 hard
victims x 8 don't-reveal masks x (1 + wt*wv)^3 over the 3 free-Intent ordinals (decline, or take with wt = 3 picks x
wv = 2 victims) = 2 x 8 x 7^3. Free-Intent decline alone: 2 x 8 = 16. Every INT-9 prune: 1 (unit-tested at an
unlimited root).

**Fixes found while validating (not in the WIP):**
- **Lake of the Dead shared a land signature with a basic Swamp** (`land_sig` had no term for
  `etb_replace_sacrifice_land_subtype`), so with both in hand only the first in hand order was ever enumerated, at any
  budget. Now in the signature; only Lake sets the param, so every other deck is byte-identical (smoke). Found by the
  P1 Swamp + Lake test.
- **The Demon timing judged the pre-chain board.** The activation is emitted before the plan's casts resolve, so a
  board short of six (gi126, gi119, gi5, gi30, gi32: INT-8 chained into Jetmir before combat and won a turn earlier) or
  six attack-ready Apostles with more to come from a hand Apostle (gi97) read "no Jetmir gain". Now Jetmir stays offered
  in main 1 whenever a chain can still add bodies (fewer than six on the board, or a sac-named creature in hand); after
  combat the devotion Demon. This is a 2-way searched branch, an amendment to the USER's rule -- SURFACED below.

**F.3 waves: DELETED.** Units/game ~0.33-0.47x INT-8 and no game over 60 s, so `RippleLateWaveAdmitted` /
`RippleDeepWaveAdmitted` and the 0x11B8 plan-cache mix are gone: the full ripple menus are admitted at every budget
under a decision root (`RippleFullMenusAdmitted`). Under the shipped provider they are never built; sanity digests and
units were byte-identical before / after the deletion. Only the `MTG_UNPRUNED` audit arm reaches them (at full width:
a d8 b0 unpruned single game ran > 5 min and was stopped, my own probe).

**Measurement** (Release, `MTG_PGO=0`, binary `mtg.int9d` = HEAD src, one pooled 1,004-game batch at 24 threads;
INT-8 = `build/scratch/shadowborn/int8/ab/g_int8`; sanity "before" = `mtg.base`, play byte-identical to INT-8). Avg =
the batch's (non-wins penalised). Both free-Intent arms:

| cell | games | avg before | avg INT-9 | faster / slower | units/game before | units/game INT-9 (alt arm) |
|---|---|---|---|---|---|---|
| d3 b10 seed 1001 (sanity) | 50 | 5.420 | 5.060 | 17 / 2 | 69,883 | 30,925 (30,966) |
| d5 b20 seed 1001 (sanity) | 50 | 5.400 | 4.980 | 19 / 0 | 108,940 | 51,468 (51,373) |
| d3 b10 seed 7001 (held-out) | 200 | 5.445 | 5.165 | 55 / 8 | 119,059 | 39,614 (39,697) |
| d5 b20 seed 7001 (held-out) | 200 | 5.390 | 5.055 | 60 / 3 | 188,887 | 70,746 (70,823) |

- **Free-Intent arms are identical in turns** (195 / 196 / 50 / 49 identical digests). Real-resolution free casts in the
  batch: Apostle 11,930 taken, Dark Ritual 64, Profane Tutor 4, Thrumming Stone 0; Diabolic Intent 7 taken (alt arm)
  / 19 declined. Recommendation: keep the default (never), as the USER leaned.
- **No SLOW-GAME (> 60 s).** Non-wins: INT-8 gi103 (d3), gi103 + gi125 (d5); INT-9 gi103, gi117, gi120 (d3), gi103,
  gi120 (d5).
- **Every slower game at `--depth 8 --budget-ms 0`** (INT-9 binary; INT-8's best shipped turn in brackets):
  gi32 5 [5], gi55 4 [4], gi117 6 [8], gi120 6 [7], gi150 5 [5], gi179 5 [5], gi193 5 [5], sanity gi8 5 [6], sanity
  gi35 5 [5] -- all recover; budget-bound (the same line, found with more search). **gi97 does NOT: 5 [4]** at d3 b10,
  d5 b20 and d8 b0.
  - *Why can they not run the same line?* INT-8: T1 Apostle, T2 Diabolic Intent eating it (fetches the Stone), T3 Lake
    + Stone + Apostle (4 reveals, then the remaining triggers NOT revealed), T4 Swamp + two Apostles into the Stone +
    Jetmir -> T4. A per-hook probe (temporary `MTG_SB_HOOK_OFF` mask, removed) restores T4 at d3 b10 and d8 b0 only
    when `RippleFixed` is off -- not the victim order, body code, timing filter, kill cut, P1 or lethal cut. The
    likely sub-answer is the reveal: INT-8's T3 stops revealing, which keeps the library order and the T4 Swamp draw
    that pays for the T4 chain; always-reveal bottoms those cards. Not isolated further (the reveal and take answers
    share the hook).
  - *Is there a similar line that should win on the same turn?* Not under always-reveal: the unbudgeted d8 search finds
    T5 as the best there.
- **No-land passes** (`build/scratch/shadowborn/rc8/noland.py`, held-out d3 b10): INT-8 26 games / 32 passes with this
  scanner (the stricter rc8 list: 19 / 23); INT-9 9 games / 11 passes, **all Lake-alone holds** (the USER's burst-turn
  exception), 0 with a free land in hand.
- **Unbudgeted d3 singles** (seed 1001+g, gi0-3, in the pooled batch): gi0 12.9 s / 668,933 units, gi1 8.8 s / 501,350,
  gi2 12.6 s / 644,140, gi3 0.23 s / 6,354 (INT-8: gi0, gi1 > 10 min; gi2 72 s / 3.45M; gi3 2.2 s / 94,785). No longer
  degenerate; ready for the Stage 4-bis backstop rows.
- **The turn after going off** (E.1; `MTG_TURN_CENSUS`, single-threaded, sanity d3 b10): units charged on the win turn,
  mean 1,210 (INT-8) -> 1,541 (INT-9), max 17,635 -> 22,436; 1.7% -> 5.0% of a game's units (the game total fell 2.3x).
  The kill turn was already cheap: the earlier turns' full-line searches serve it from the game-line cache. Its share
  rose because everything else shrank. When the cut fires, the frame holds the verified kill plus the land family's
  idle baseline (2 plans).

**Tests:** `test_shadowborn_int9.cpp` (fixed answers, victims, Lake, body code, timing incl. 7 / 10 / 11 / 10 + 2
Harpies, the chain cases, sick turn main 1 only, the 13 / 12 and 2HG 14 / 13 kills, the kill collapse, one enumerator
variant, the F factor probe, P1 incl. Swamp + Lake); INT-4 runs with the timing filter off; INT-5's wave tests now
assert full menus at any budget under a root. **Unit 2517/2517. Scenarios 398/398. Smoke** (not accepted): 161
passed, 4 failed -- the pre-existing Merfolk d0/d3/d5 + merfolk2hg d3 (merfolk d3 digest 16290806e85e1728); every other
case byte-identical (this deck is not a suite case yet).

**Decisions for the USER (surfaced, not blocking):**
1. **The chain exception to the Demon timing rule** (mine): when a chain can still add bodies this turn, Jetmir stays
   offered before combat and the search compares it with the Overlord after combat. Without it, 6 held-out games lost a
   turn. Keep it?
2. **gi97**: the always-reveal pin (CONFIRMED) costs this game a turn at every budget. Accept it, or keep the reveal
   searched when a granter is cast this turn with a land-light hand? (I recommend accepting: 1 of 200, and the pin is
   what keeps the chains narrow.)
3. **Free Intent**: never (default) vs take-iff-expendable measured identical in turns. Keep never?
4. gi117 / gi120 are new non-wins at the shipped budget (both win T6 at d8 b0). Budget-bound, not a lost line.

## INT-9 follow-ups (2026-11-03): rulings, gi97, the INT-8 larger test, the rebase, suite entry

**USER rulings:** the chain exception to the Demon timing rule is kept ("If we have a Shadowborn in hand, we can play it
and potentially drop more off thumming stone ... particularly if we sacrifice the summoning sick ones"); free Diabolic
Intent is never taken ("let's decline it"); always-reveal is fine ("arguably not correct only in 1v1"); the lethal check
stays; gi97: "dig into the actual example and verify that it is clairvoyance. That is th only reasonable reason to
ignore it."

**gi97 is NOT clairvoyance; the pin is amended (the ripple KNOWN STOP).** INT-8's T3 (seed 7001 gi97, d3 b10): Lake +
Stone + Dark Ritual (ripple keeps a second Ritual) + Apostle, whose chain reveals 23 times -- every card of the library is
revealed and bottomed, then the chain keeps revealing with no Apostle left, and INT-8 stops exactly when Swamp #46 is on
top (T4: Swamp, sac six, win). At that point every library card has been seen and placed by the player, so the stop is
a decision on known information. (The blind ensemble could not settle it: blinding also re-decides the London bottom
and the T1 play, so 7 of 8 salts were different games; the one with the same keep cast its Ritual on T1.) Amendment, in
the RippleFixed path only: the first copy of an F1 name also carries `ripple_reveals = kRippleKnownStop + k`, "reveal
every trigger, but once the library holds no copy of the name AND is wholly the placed bottom block, reveal only k
more"; the enumerator emits one variant per DISTINCT library order a stop after k known reveals leaves (a dry-run pass
records the orders; an order equal to an earlier k's or to ALL's is not emitted, so all-Swamp tails add nothing).
Commit e8b1f26fd (pre-rebase hash), test `INT-9 known stop`.
- gi97: T4 again at d3 b10 and d5 b20.
- Held-out A/B, seed 21001, 800 games per cell, pooled per binary (build/scratch/shadowborn/int10b/):

| cell | INT-9 | + known stop | faster / slower | units |
|---|---|---|---|---|
| d3 b10 | 5.1063 | **5.1013** | 8 / 4 | 23.71M -> 25.11M (+5.9%) |
| d5 b20 | 5.0375 | **5.0350** | 6 / 4 | 42.53M -> 45.30M (+6.5%) |

- Every slower game at d8 b0 (amended binary; INT-9's shipped turn in brackets): gi1 5 [5], gi189 5 [5], gi201 5 [5],
  gi241 4 [4], gi492 5 [6/5], gi510 5 [5], gi653 4 [4] -- all recover (budget-bound). Better overall, no lost line: adopted.

**INT-8 larger test (USER: "show that it helps slightly ... adopt it if it can match the average with no
unrecoverables").** Seed 21001, 800 games per cell, INT-7 vs INT-8 binaries (build/scratch/shadowborn/int8/ab2/): d3 b10
5.4350 -> 5.4337, d5 b20 5.3587 -> 5.3625 (batch avg, non-wins penalised); units -1.2% / -0.6%; slower 6 / 10, faster
8 / 8. INT-8 alone is neutral within noise. **Every INT-8-slower game, replayed on INT-9 (which carries every INT-8 rule)
at the cell's own budget, wins at INT-7's turn or earlier**: d3 gi141 5 [5], gi214 4 [5], gi438 4 [5], gi454 5 [5], gi632 4
[4], gi752 4 [5]; d5 gi141 5, gi214 4, gi437 5, gi454 5, gi483 5, gi494 4, gi604 4, gi632 4, gi739 6, gi752 4 (all <= INT-7);
d8 b0 likewise (all <= INT-7). So no INT-8 rule removes a line INT-7 found: each rule meets the bar in its INT-9 form,
and the per-rule ablation is not needed (the slower INT-8 games were the depth-1 tail noise RC-8 described, which
INT-9's narrower menus removed).

**Rebased onto origin/phase-1-2-deck-analyzer (2406875c9)**: conflicts were all additive (upstream's upkeep_tutor pin
beside our demon-trigger pin, the plan-cache traits key beside the ripple-wave mix later deleted, two heurarm slots);
sizeof(GameState) 1272. Unit: 2542/2543 -- `mana oracle: a memoised continuation's Blood source is not served to its
tapped twin` fails its arm-0 "defect for the record" check (covered == checked with the fix OFF); see smoke below.
Scenarios 400/400.

**INT-10 (playout P3) is NOT run tonight**: play is frozen here for the mulligan generation (the brief stays in
logs/shadowborn/int10-brief.md for later).

## Mulligan profile + value leaf (2026-11-03 overnight chain)

- **Mulligan profile SHIPPED** (e9f0f100d; fast R30, K=15, generation setting d2 b1 screened by mullgen): keep -0.2715t
  (4/4 seeds, t -9.6), confounded bottoming -0.0745t (4/4, t -13.1).
- **Value leaf: open.** USER 2026-11-03: "No leaf is okay, but heuristic ladder is not." -- the deck has no sidecar, so
  it plays the default heuristic ladder (phase E's `live` arm). USER: it is ruled out because it is an option that
  SHOULDN'T beat the alternatives -- it wastes time on early rollouts that are thrown away -- and it stays as "a good
  sanity test that forces us to evaluate things like in this case": the CONTROL arm in every screen. Losing to it on one
  axis is normal (escalation can cost more); losing on BOTH quality and cost, as nlesc / nlfit do here, is a red flag. Phase E vs live,
  8 paired seeds x 1000 games, d5 b20: staged model -0.0091t at 1.89x units; leafless nlesc +0.025t at 1.44x; leafless
  nlfit +0.045t at 1.46x. USER: "If we find the heuristic ladder that we start with beats other options that points to
  a bug in our implementation" -- under investigation (the leafless shapes lose on gi4 seed 600004 at the T1 decision:
  the single pass commits an unverified depth-3 "win 5" dump-everything line; the ladder commits a depth-2 T4 line).
- Owed: overnight ENTRY -> FULL resize (accept with the keep table absent), then the smoke/regression GT re-accept
  for the keep-table adoption.

## Overnight rows resized ENTRY -> FULL (2026-11-04)

The keep table shipped, so the overnight ENTRY set expanded per the regression-testing skill's floor: d5 b20 play
rows 4 x 300 -> 4 x 425 (Hinata's floor), the four d3 b10 400-game rows -> one 125-game sanity row (s4004), d0 rows
unchanged. No overnight GT existed yet for Shadowborn; the next periodic overnight run accepts these rows as new.

## s7 budget miss (2026-11-04)

USER: *"If it doesn't find that relatively straightforward line at a decent budget we shold probably dig to see if
there is a way to reduce the cost so that it can."* Reference `claude_s7_gi6` (force_mulligan 1:16): the human wins
T5, the shipped search (d5 b20) T6. Branch `sb-s7cost` (local), scratch `build/scratch/sb-s7cost/`.

**0. The hand-off tool was not the shipped engine (fixed, e66dddfc0).** `--choices-then-auto` never called
`SetSearchPostCombat`, so on every second-main deck a hand-off ran without the searched second main: s7 handed off at
T1 frame 0 won T5 at b20 while the real game (CLI and `--batch`) wins T6. Fixed at the hand-back; the T1 hand-off now
reproduces the batch (b20 T6, b80 T5). Every earlier `ref_handoff.py` / `ref_handoff_sweep.py` result on a second-main
deck (Shadowborn, Fungus, AntiLifegain, Kitty, ...) measured the single-main engine. Same commit: the hybrid root's
census row and `MTG_PLAN_DUMP` keyed the phase off `second_main` (pre=0 / the second-main menu at every second-main
root); now pre-combat. Diagnostics added (off by default): `MTG_PLAN_DUMP_STATE`, `MTG_LA_HIST`,
`MTG_CLAUDE_STOP_AFTER_TURN`.

**1. Localised: the T2 pre-combat main.** Board: Swamp + Apostle; hand Profane Tutor, Demonic Tutor, Diabolic
Intent, Lake of the Dead, Apostle, Abhorrent Overlord. Hand-offs from that board, differing only in budget: b10..b40
play Lake + Apostle (T6), b60+ play Lake + suspend Profane Tutor (T5). From the T3 board after Lake + Apostle the
game is T6 at b20, b80, b320 and d8 b0 -- T5 is gone. After Lake + Profane, b20 wins T5. After the human's T2
(Apostle, Lake held), d8 b0 wins T5 but b20 and b80 do not (a second T5 line, not the one the budget finds).
Clairvoyance: the Profane line casts Profane off suspend at T4 upkeep (fetching Thrumming Stone) and its T4 draw (Dark
Ritual) comes after Demonic Tutor's T3 shuffle, which the search sees; the human's line needs no library knowledge.

**2. Why b20 misses it: the ladder stops at depth 1.** T2 pass 1 (depth 1) costs 5,789 units and scores both T2
plays T6 (the horizon leaf cannot see the T4-upkeep Profane). The start gate refuses pass 2 with 12,211 of 18,000
units left. Pass 2 (T2 and T3 searched) finds T5 and costs 91,013 units at b60 (148,335 at b400: the late waves admit
more at a larger budget), i.e. 16x pass 1 and 7.5x the whole b20 budget.

Where pass 2's 91,013 units go (census delta b60 - b20 at the T2 root): la_cand 47,438, rollout_step 14,199,
la_bp_wave 14,191, greedy_fallback 13,473, fs_bp_node 789, fs_main2 690, fs_pre 233. `MTG_LA_HIST` on one T2 child (the
Profane line's T3 node, d1 unlimited): EVERY la_cand candidate is inside the horizon rollouts (rnest=1), i.e. the
engine-default 1-ply leaf (`MTG_FD_LEAF_DEPTH` default 1; Shadowborn's profile sets no `search_leaf_depth`):

| leaf turn | m1 calls / cands | m2 calls / cands |
|---|---|---|
| T4 | 155 / 4,393 | 2,134 / 8,242 |
| T5 | 39 / 512 | 441 / 1,433 |
| T6 | 9 / 45 | 19 / 50 |

The leaf's own second main is searched at the leaf's ply for every first-main candidate (USER doctrine: m2 searched
wherever m1 is), so each leaf turn is (m1 menu) x (m2 menu) playouts. Per T2 child, the T3 depth-1 subtree costs (d1
unlimited) 12.6k (Lake + Apostle), 14.2k (Apostle, Lake held), 22.1k (Lake), 21.4k (Lake + Profane), ~1k (each
DT/DI -> Stone). Leaf transposition hits 75% (19.8k lookups) -- a hit already skips its rollout but pays its 1 la_cand
unit. Arms at b400, T2 pass costs: base pass 2 148k; `MTG_NO_M2_SOLVE=1` (upper bound only, never a proposal) pass 2
73.8k; greedy leaf pass 1 122, pass 2 3,440 (T6), pass 3 143k (T5).

**3. Waste seen in the search's own T3 menu (Profane line; `MTG_PLAN_DUMP_STATE`).** 52 plans, 46 distinct states.
(a) 24 are Profane Tutor's T4-upkeep pick variants (`upkeep_land_search` = 1, 2 on top of the front): the T3 menu is
x3 for a pick that, at a last-ply T3 node, resolves beyond the horizon. (b) 6 are Lake of the Dead SacForMana plans
whose entire output is left floating with nothing left to spend it on (e.g. `PlayLand:Swamp + SacForMana:Lake`,
pool 5, nothing cast), each strictly dominated by the same plan without the activation (a Swamp and an untapped Lake
kept). (c) 6 exact STATE-DUPs, already folded. NOT implemented here: neither closes a 7.5x gap; (b) needs a soundness
proof against breakpoint continuations that could spend the float, (a) changes what the horizon sees at every budget
(a play change needing the A/B, and arguably the USER's call). Both are the next collapses to build.

**4. The lever that does close it is a SETTING (USER decision, not adopted): the greedy horizon leaf.** Held-out A/B,
seeds 920000-920999, d5 b20, 1000 paired games, one pooled batch (arm = manifest `leaf_greedy_from_depth: 1`, which
is `search_leaf_depth: 0` for every pass):

| arm | avg turn | units/game | thread-ms |
|---|---|---|---|
| shipped (1-ply leaf) | 4.8600 | 21,082 | 883,025 |
| greedy leaf | 4.8100 (-0.050, t -4.69; 68 faster, 19 slower) | 24,198 (+14.8%) | 944,123 (+6.9%) |

All 19 slower games recover at d8 b0 under the greedy leaf (each <= its shipped-b20 turn; gi236 6 -> 5). s7 at b20:
T5 (T2 pass 3, 61,822 units, finds the Profane line). `ref_bench --deck shadowborn_apostle`: shipped 1/10 short (s7
6 vs 5), greedy leaf 0/10 short (search avg 4.2 -> 4.1). More units, because the cheap passes let the ladder reach
depth 3 and the overrun ceiling lets that pass finish. Caveats for the decision: the keep table was generated under
the 1-ply leaf (a leaf change is a play change; Rule 0 of mulligan-profile.md applies to whether to regenerate), and
the per-deck leaf K is what the value-leaf pipeline's leaf calibration measures -- Shadowborn's value leaf is still open.

## Lake fold + Profane fetch timing (2026-11-04)

USER rulings: on the Lake waste, *"For 2 I agree that it should not be sacrificing the land unless we need it. We
should sort that out if possible."* On Profane Tutor, *"The card that the tutor fetches should not be staged upfront
particularily for Profane Tutor. For other things that happen immediately it potentially could be, but I think we
have been inconsistent about this, which isn't great."* Later the same day, on Lotus Bloom: *"It should be land ->
Dark Ritual -> other spells. Lotus bloom should be sacrificed on demand by the mana system. not a direct activation.
Land drop should happen main 1 unless we searched it up in main 2."* Branch `sb-lakeprof` (local). Commits
48448ddad, 24774b850 and the commit carrying this section. Scratch is `build/scratch/sb-lakeprof/`.

**1. `MTG_IDLE_CRACK_FOLD` (default ON; a SOUND identity fold).** Take a plan C holding a land sac-for-mana crack
(Lake of the Dead; also Dwarven Ruins and Svyelunite Temple) and its sibling S, which is C without that crack and
equal in every other field. If activating the crack on S's end state gives exactly C's end state (`BuildDedupKey`), C
is dropped (`FoldIdleLandCracks`, TurnSolver.cpp). The crack is a mana ability. S still has the untapped Lake and the
Swamp, so any later decision of the phase can crack from S's state; if nothing does, the float empties and S keeps a
Swamp. A crack whose float pays a cast keeps C, because S paid differently, so the keys differ. This also covers
"crack, Demonic Tutor, continuation casts the fetch". Without a sibling the plan is never folded. A two-crack plan
chains onto the one-crack plan, then onto the no-crack plan.
* **Where it runs:** on the full land-tagged menu (`EnumeratePlansWithLand`, the no-drop path), before breakpoint
  variants and axes. A copy inside `EnumeratePlans` folded slightly more (units 22.09M vs 22.23M per 1000 games)
  but cost about +16% thread-ms, so it was removed.
* **What it is not:** the USER's on-demand payer rule for Lotus Bloom (and, by the same shape, Lake). That one is
  written up as deferred work in `docs/design/sac-for-mana-on-demand.md`, not built. This fold is its
  sound-identity subset and stays correct until the payer replaces the action.

**2. `MTG_PROFANE_RESOLVE_FAN` (default ON): Profane Tutor's fetch is decided at resolution.**
* **How the pin was created:** fan (3) in `AppendSubdecisionAxes` gave every base pre-combat plan with a suspended
  tutor arriving next upkeep one variant per fetch rank (`Plan::upkeep_land_search_choice`, the Search for Tomorrow
  stream). The T3 menu was x3 for a pick resolved at T4's upkeep, often beyond the horizon. That was the ledger's
  24 of 52.
* **What it is now:**
  * No main-phase variant is created.
  * The fetch is decided where the search CROSSES into that upkeep. `BoundaryFan`, the Defense of the Heart and
    upkeep-tutor route, crosses once per rank of the resolution-state candidate list: the provider's ranked,
    USER-table-restricted `TutorCandidates`, deduped, capped by `TutorSearchWidth`, front first.
  * It records the winner on the plan of the turn that crossed: `Plan::suspend_tutor_choice` ->
    `GameState::scripted_suspend_tutor`. This is packed per suspended-tutor search at that upkeep, with a ripple
    free copy's own search before the arrival's.
  * The second main is never skipped while a fetch is pending (the DotH rule).
* **Beyond the horizon** (narrowed playout, d0 runner, constant-leaf edge) the provider's front is the permitted
  policy.
* **Human play** is untouched: the fan and the old axis are both off there. `CastOffSuspend` with no pin asks the
  `tutor` prompt at the upkeep.
* **Census (`MTG_STUT_AXIS_STATS=1`), 200 held-out games:** searched-frame uncovered picks with more than one
  candidate = **0**, and playout = 58,604.
* **real-upkeep = 1.** One executor upkeep (seed 940184 T5) resolved an unpinned fetch. The turn that crossed
  committed a line whose crossing was beyond its horizon (an early ladder pass), so the front was taken, which is
  the permitted playout pick. The old design priced it through a variant's leaf instead. **Open for the USER:** a
  real-game upkeep search node, if even this case should be searched.
* **Lever off:** `=0` restores the old pin.
* **Unit tests:** `test_shadowborn_lakeprof.cpp`; the INT-1 and INT-4 Profane tests now use the new pin.

**3. Audit: when each tutor-like pick is decided** (read-only; nothing else changed):

| Effect (cards) | Where the pick is fixed | When it resolves vs. that plan | Verdict |
|---|---|---|---|
| To-hand / to-top / Nth tutors cast in main: Demonic Tutor, Diabolic Intent, Idyllic, Eladamri's, Beseech, Gamble, Long-Term Plans, Sylvan Scrying, Steelshaper's, Open the Armory, graveyard regrowths | `Plan::tutor_choice` RANK (`MTG_TUTOR_AXIS_RESOLVE` default ON), `AppendSubdecisionAxes`; consumed in `PerformTutor` | inside the plan's apply | sound identity (rank re-ranked at resolution) |
| Creature ETB / Vial-put tutors: Stoneforge, Ranger-Captain, Recruiter, Matron, Harbingers | same axis (put-tutor branch) | ETB inside the apply | sound identity |
| Tutor to battlefield: Natural Order, Chord, Green Sun's Zenith | same axis; `PerformTutorToBattlefield` | inside the apply | sound identity |
| Crop Rotation (`tutor_land_to_battlefield`) | fanned by the same axis | inside the apply | **likely bug**: `PerformLandTutorToBattlefield` never reads the pin (all variants take the front; the pin leaks to the next tutor in that apply) |
| Wishes (`wish_from_sideboard`) | same axis | inside the apply | sound identity |
| Fetchlands | `Plan::fetch_target` by name | at the land play inside the apply | sound identity |
| Hard-cast land searches, Titan ETB, Evolving Wilds-style cracks | `Plan::land_search_pins` ranks | inside the apply | sound identity |
| Primeval Titan attack trigger, in-combat Khalni crack | `Plan::attack_land_search_choice` | same turn, at declare-attackers | pinned for a later step (mild; rank, nothing new in goldfish between main 1 and attackers) |
| **Search for Tomorrow off suspend** | `Plan::upkeep_land_search_choice` (main-phase fan (3)) | NEXT upkeep | **timing defect** (Profane's old shape) |
| **Emeria, the Sky Ruin** | `Plan::upkeep_gy_reanimate_choice` | NEXT upkeep | **timing defect** |
| **Abiding Grace** | `Plan::grace_choice` | end step (main 1's pin spans combat + main 2) | **timing defect** (mild, rank) |
| Birthing Pod; Shadowborn's Demon fetch (`SacNamedTutorPut`); ripple free tutors; graveyard picks (Haven, Ooze, Garth) | `Action::tutor_target` / `ripple_free_tutor` per action | inside the apply | sound identity |
| Upkeep tutor-to-top (Enlightened / Worldly at next upkeep) | `BoundaryFan` -> `upkeep_tutor_choice` | NEXT upkeep, before the draw | decided at the crossing |
| Defense of the Heart | `BoundaryFan` -> `doth_put_choice` | NEXT upkeep | decided at the crossing |
| **Profane Tutor (this change)** | `BoundaryFan` -> `suspend_tutor_choice` | NEXT upkeep | decided at the crossing (was a timing defect) |
| Dragonstorm put order; Ranger of Eos multi-tutor; Light-Paws; Regrowth auto-pick | not searched: provider pick at resolution | inside the apply | not searched (in-window provider pick; outside this audit's pin question, flagged) |

Every fan is off in human play; the person is prompted at resolution (`g_play_tutor_chooser`, the sac-tutor chooser). Beyond the horizon every unpinned pick is the provider front.

**Recommended consistent rule (USER decision):** a pick may be fanned inside a plan only if it resolves inside that
plan's apply, and then as a RANK computed at resolution. Any pick resolving after the apply returns is decided where
it resolves: combat, end step, or next upkeep via the BoundaryFan. By that rule, Search for Tomorrow off suspend,
Emeria and Abiding Grace are timing defects of Profane's shape, and Titan's attack trigger is a mild one. The audit
also found Crop Rotation's fanned pick is never read at resolution (a likely bug) and several in-window provider
picks that are not searched (Dragonstorm's put order, Ranger of Eos, Light-Paws). None was changed.

**4. Validation.**

Everything below was run at d5 b20 with the deck profile (`ignore_play_profile`, the shipped play shape). Units are
deterministic; thread-ms ran on a shared box.

* **Unit tests:** `mtg-test`, 2567 cases pass. One test, `test_vampires_madness`, hit a cards.json read race while the
  file was being edited; it passes when re-run.
* **Scenarios:** `bash test/scenarios.sh`, 401/401.
* **Held-out A/B:** one pooled `mtg --batch`, 4 arms x 1000 paired games, seeds 940000..940999, arms set by per-job
  heurarm flags. Runs: `build/scratch/sb-lakeprof/ab2/`.

| arm | avg turn | paired vs base | units/game | thread-ms |
|---|---|---|---|---|
| base (both levers off) | 4.8990 | | 22,619 | 749,232 |
| lake only | 4.8990 | 0 faster / 0 slower | 22,226 (-1.7%) | 780,882 (+4.2%) |
| profane only | 4.8920 | -0.0070 (t -1.94; 8 faster, 2 slower) | 22,744 (+0.6%) | 764,324 |
| both (shipped default) | 4.8900 | **-0.0090 (t -2.33; 10 faster, 2 slower)** | 22,370 (-1.1%) | 811,075 (+8.3%) |

* **Slower games:** the same two in both profane arms, gi427 and gi842 (4 -> 5). At `--depth 8 --budget-ms 0` both
  win T4 under the new engine, equal to the base engine's d5 b20 turn and its d8 b0 turn. Both recover.
* **s7 at b20** (seed 7, gi6, force_mulligan 1:16): still T6 on both arms (same decision digest). Units 34,695 ->
  31,986 (-7.8%). At b80 both arms win T5. As the s7-miss section predicted, neither collapse closes the 7.5x
  pass-2 gap, so the greedy-leaf decision stays open.
* **Thread-ms:** the lake fold's +4% is the price of its extra trial applies. Two more applies per crack plan
  against a sibling, on Shadowborn and the Dwarven Ruins decks only.

* **Smoke** (`smoke.final.log`; GT was NOT accepted): 6 rows moved, 163 pass.
  * shadowborn d5 s1001: 4.96 -> 4.94 (gi8 7->6, gi58 5->4).
  * shadowborn d3 and shadowborn2hg d3: same score, play differs.
  * breaching d3, d5 and 2hg d5: same score, play differs (Dwarven Ruins / Svyelunite Temple: the crack fold).
* **Regression** (`regr.log`): 9 rows moved, 262 pass.
  * shadowborn d5 s3003: 4.8667 -> 4.8400 (gi15 8->5, gi43 6->5).
  * shadowborn d3 and d5 s2002: same score.
  * breaching x3: same score.
  * Viewer protocol: 0 play-drift / 0 enum-gap / 0 board-diverged. REQUIRE-BEAT passes.
* **Three rows of decks without these cards also moved:**
  * th d5 s2002: same score.
  * hinata d0 s2002: 6.848 -> 6.847, gi60 no-win -> T8.
  * bruna d0 s2002: same score.
  * th d5 reproduces with BOTH levers off, so it is not either lever. It is either a non-lever side effect
    (struct/key shape) or GT drift that predates this change.
  * **ATTRIBUTION PENDING:** this needs the base-commit binary (b379c41b4), which is being built in a scratch worktree.
    See `logs/lakeprof-handoff.md`.
* **Every moved Shadowborn/Breaching row is net <= 0.** `ref_bench --deck shadowborn_apostle` has not been run yet
  (pending, same hand-off).

**Open for the USER:**
1. Adopt the two levers. Both are default ON.
2. Whether an executor upkeep should be its own search node for an unpinned fetch. This is the 1-in-200 real-upkeep
   case above.
3. The consistent pick-timing rule. The rows it would move are Search for Tomorrow, Emeria, Grace and Titan's attack.
4. The Crop Rotation pin bug.
5. When to build the on-demand sac-for-mana payer (`docs/design/sac-for-mana-on-demand.md`).

### USER rulings on the Lake/Profane decisions (2026-11-04, verbatim)

1. Main-phase split scope: *"With no search that is fine."* (searched play only; the d0 runner keeps main 2.)
2. DT vs Intent order: *"Yes, this order doesn't matter. Stick with what you have."*
3. Adopt the Lake fold + Profane change: *"Sure, if there is no performance drawbacks that sounds like an
   improvement."* OWED before landing: a clean timing A/B (the pooled A/B showed thread-ms +6.7% to +18.6% on the
   fold arms against -1.7% units; the arms disagree with each other, so it is noise or a real fold cost to fix).
4. The executor's upkeep falling back to the provider's Profane pick: *"We should search and use a heuristic on
   that decision like any other."* OWED: make that upkeep a searched decision (provider order), not a fallback.
5. Search for Tomorrow / Emeria / Abiding Grace / Titan attack trigger: *"Yes, I agree that this is a separate
   change, but really it makes the most sense to decide what you need in the context of the situation at the
   time it is cast."* OWED as a separate change: decide at resolution.
6. Crop Rotation: *"If that is in the engine there should be search, but the only deck with the card (Creature
   Giving) has a serious pruning heuristic which should remain."* OWED: wire the searched pick at resolution;
   keep Creature Giving's prune.
7. On-demand Lotus/Lake payer timing: *"Any order is fine from my perspective, so after seems good."* After the
   main-phase work (`docs/design/sac-for-mana-on-demand.md`).
8. Greedy leaf: *"When done with some other work we should measure and decide. I would like to compare it to
   budget changes etc., since it seems like the deck is budget-starved."* OWED: one comparison of leaf0, the
   leafless shapes and plain budget increases (same cost axis), after the main-phase and Lake/Profane cuts.
9. Shape for this deck: *"emul is probably good enough for this deck. Any nlfit work is more focused on making that
   a useful tool."* OWED: adopt `logs/vlq_shadowborn_apostle/Shadowborn Apostle.value.EMUL.json` as the deck's
   sidecar via the DECISIONS.txt route (copy, delete the E7_playset / E7b_trust / E6_skipcal markers, re-run
   `valueleaf.sh run`), alone on the box and after the main-phase play change lands (its emul_r is fitted to play).
   The nlfit pricing fix continues as a FLEET tool (judged across decks, not against emul on Shadowborn).
10. Lake of the Dead / Lotus Bloom route: *"Yes, it should be on-demand and only done that way."* So the on-demand
    payer (`docs/design/sac-for-mana-on-demand.md`) is the ONLY route for these cracks: the SacForMana plan action
    for both cards is retired, not kept beside the payer. The Lake fold (`MTG_IDLE_CRACK_FOLD`, default off) is not
    made cheaper; it is deleted with the action when the payer lands. Order unchanged: after the main-phase work.

## 2026-11-08 mana gate baseline

The first current-source gate (`pass1-shadowborn-apostle`, games 0-4999, source tree
`90af1f5c88b8`) found 11,100 issues: 2,890 APPLY-DROP-ORDER, 999 APPLY-DROP,
6 ENUM-GAP, and 7,205 UNMODELED. ORACLE-GAP was zero, and oracle-on and
oracle-off play were byte-identical. The full signatures and repro commands are
in `logs/mana_clean/pass1-shadowborn-apostle/report.md`; counts are recorded in
`test/mana_clean.json`.

The largest order signatures require a hand cast before the Lake of the Dead
crack and Profane Tutor suspend. Both apply worlds currently pre-apply
SacForMana and Suspend before hand casts, so they cannot realize that order.
The on-demand payer in `docs/design/sac-for-mana-on-demand.md` is the user-approved
route for Lake and Lotus Bloom. A local cast-order workaround would leave the
same pre-pass mechanism in place and conflict with that ruling. The remaining
payment drops and the action-count cap need a fresh census after the on-demand
payer lands.

## 2026-11-10 mana oracle order coverage

The later 5,000-game gate (`lastweek-20261110-next`, indices 15000-19999,
source tree `740823c3f482`) reports 6,846 Shadowborn issues: 6,167 UNMODELED,
550 APPLY-DROP-ORDER, 83 APPLY-DROP, 35 ORACLE-GAP, and 11 ENUM-GAP.
The UNMODELED reason is specifically `too many actions to try every order`.
It means the oracle cannot prove that a dropped plan is impossible; it is not
evidence that the engine missed a payable plan. The order search previously
stopped at five movable actions even when identical copies made the number of
distinct orders small. It also started `next_permutation` at the plan's order,
which could skip lexicographically earlier orders while claiming to have tried
all of them.

`OtherOrders` now sorts the movable actions before permutation, skips duplicate
orders, and admits a plan only when its exact multiset permutation count is at
most 180. It still gives no impossibility verdict beyond that bound. This is
an oracle-only coverage change; it does not alter the search menu or payer.
The targeted gate seed 30015115 shows the narrowed proof: its six-action line
with two Dark Rituals and two Apostles (180 distinct orders) changes from
UNMODELED to PHANTOM, with zero `too many actions` flags in that one-game run.
Seed 30015427's six-action line with only one repeated pair (360 orders)
remains UNMODELED, as do its six-unique-action plans. These are exact
order-and-payment no-claims for the audited plan, not fixes to the 550 known
payable order misses or 83 payment drops.

The fresh post-change gate `lastweek-20261110-shadowborn-order` covered games
20000-24999 at source tree `1d6ab1a69b4b`. It reported 5,429 issues:
4,759 UNMODELED, 554 APPLY-DROP-ORDER, 95 APPLY-DROP, 16 ORACLE-GAP, and
5 ENUM-GAP. Oracle-on and oracle-off play were byte-identical. The 4,759
UNMODELED flags still carry the `too many actions to try every order` reason;
many top signatures have more than 180 distinct orders even after duplicate
cards are folded. This fresh game block is different from the preceding one,
so the lower total does not by itself measure the fix's effect. Seed 30015115
is the paired evidence that the exact-order proof recovered a previously
unmodeled line. The remaining payable order misses and payment drops still
need engine work before another clean gate can pass.

## 2026-11-10 night: the order proof completed, the early-crack pin, hidden ripple rituals

Gate `sb-victimclaim-20261110` (games 25000-29999) left 1,658 UNMODELED, all "too many actions to try every
order" (360 distinct orders: 1,035; 720: 333; 420-2,520: 290). The PHANTOM claim now enumerates up to 5,040
distinct orders (`kPhantomOrderBound`, ManaOracle.cpp; the YES searches keep 180, the per-decision budget still
caps cost). Every top repro now reads PHANTOM -- the planner admitting a line only a lucky ripple could fund --
and none an engine miss. 24 of the 28 ORACLE-GAPs were the oracle clearing `scripted_early_ondemand_crack` on a
scripted Lake crack where the payer (Lake + Lotus Bloom, its ordinary crack) keeps it; the oracle now applies the
payer's rule (`OracleOptionConsumesEarlyCrackPin`). 5 of 10 ENUM-GAPs paid only with a Dark Ritual that Thrumming
Stone's ripple cast off the hidden library top: HIDDEN-INFO now. Analyzer-only; smoke and regression
byte-identical. Parked items and diagnoses: `docs/design/mana-clean-pass-checkpoint.md`, 2026-11-10 section.

## USER ruling on Dark Ritual vs Thrumming Stone (2026-11-11, verbatim)

Context: 163 APPLY-DROP-ORDER on gate sb-orderproof-20261110; the top decision (s30033099 T6) pays as Culling >
Apostle > Stone > Ritual > Sire, while the USER's order casts Dark Ritual (rank 1) before Thrumming Stone (3).

> For the shadowborn order, I had already specified that there should be the option to cast a shadowborn sacrifice
> before Culling the Weak if there is none currently. Ritual after stone is a bit messy. I'm not entirely against
> the idea if it is used for mana, but I don't want to provide a choice between the to help out clairvoyance. For
> example, clairvoyance can use the ritual to clear out a no-shadowborn set of 4 cards off the top. If we want the
> ritual to work like that, my recommendation would be to cast it after if mana allows and before if not.

How it applies: NOT a searched choice between the two positions. A deterministic arrangement: when the line holds
both Dark Ritual and Thrumming Stone, Ritual goes after Stone if the line pays that way (judged WITHOUT any mana from
ripple-revealed cards, which are hidden information), otherwise it keeps its rank-1 slot before. The existing
"Apostle sacrificed to Culling moves just before Culling" arrangement (the USER's earlier rule) is unchanged.
USER clarification (verbatim): *"In other words, there is no extra search for clairvoyance to abuse. It has exactly
one choice."* -- each line yields exactly ONE Ritual position; the plan count must not grow.
