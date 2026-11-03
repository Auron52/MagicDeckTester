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
