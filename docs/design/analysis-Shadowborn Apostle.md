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
