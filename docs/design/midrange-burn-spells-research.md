# Midrange Burn spell research

Live Scryfall named-card API fetched with curl, 2026-10-30 UTC. Research draft only; no cards.json or engine edits. All six costs and Oracle text below are fetched verbatim. New param names are proposals, not currently supported.

## Chain Lightning

Source: https://scryfall.com/card/dmr/113/chain-lightning?utm_source=api

```json
{
  "name": "Chain Lightning",
  "mana_cost": "{R}",
  "types": [
    "Sorcery"
  ],
  "keywords": [],
  "oracle_text": "Chain Lightning deals 3 damage to any target. Then that player or that permanent's controller may pay {R}{R}. If the player does, they may copy this spell and may choose a new target for that copy.",
  "template": "direct_damage",
  "parameters": {
    "damage": 3,
    "targeting": "any"
  }
}
```

Tier 1 for passive goldfish, otherwise Tier 3 response/copy choice. First clause maps directly to direct_damage 3 / targeting any and existing target chooser. Second and third clauses give the damaged player or permanent controller (NOT necessarily opponent) the optional RR payment, optional spell copy, and optional new target. Passive opponent never pays; however a HUMAN self/own-creature target can enable our own copy, so claiming globally inert is false. Implement shared post-hit continuation when damaged object's controller has RR, preserving source spell identity and copied-spell-not-cast distinction; copying must not trigger Eidolon/prowess/storm. Repeated copies can bounce; payment/copy/new target are real choices and need enumerated protocol options plus existing target surface. Alternatively explicitly PROVISIONAL bracket-note passive-opponent-only copy omission and disclose viewer self-target gap; not a faithful full implementation and not approved. No second-main mechanic.

## Rift Bolt

Source: https://scryfall.com/card/2x2/122/rift-bolt?utm_source=api

```json
{
  "name": "Rift Bolt",
  "mana_cost": "{2}{R}",
  "types": [
    "Sorcery"
  ],
  "keywords": [
    "Suspend"
  ],
  "oracle_text": "Rift Bolt deals 3 damage to any target.\nSuspend 1\u2014{R} (Rather than cast this card from your hand, you may pay {R} and exile it with a time counter on it. At the beginning of your upkeep, remove a time counter. When the last is removed, you may cast it without paying its mana cost.)",
  "template": "direct_damage",
  "parameters": {
    "damage": 3,
    "targeting": "any",
    "suspend_time_counters": 1,
    "suspend_cost": "{R}"
  }
}
```

Tier 3 because existing suspend-arrival sorcery resolution silently does nothing except land searches. CardDatabase params already exist and paid from-hand suspend actions are surfaced via suspend= verb. Hard cast direct damage works. ApplySuspend sets arrive_turn = turn+1; upkeep arrival precedes draw. Extend CastOffSuspend (SpellEffects.h ~19347) non-permanent branch to deal the real burn through shared target-aware resolution and FireOnCastTriggers: current code increments storm/MV but does NOT fire Eidolon, prowess, Steam-Kin, etc. Its MV stays 3 even when cast free; suspending is not casting and triggers none of these. Existing CastOffSuspend unconditionally casts; real Oracle says MAY and declining leaves the card exiled with zero counters permanently. For this deck Eidolon can make declining important; build explicit cast/decline upkeep choice and corresponding searched upkeep variants (not a forced-cast inert collapse). Expose upkeep target via existing target chooser including own targets. If all targets illegal the spell does not resolve and goes to graveyard. Ensure damage marks opponent_lost_life_this_turn so spectacle unlocks already in first main. Existing suspended_cards storage needs declined cards retained in exile without re-prompting each turn (zero-counter/no-pending-arrival state). Tests: suspend T1 costs R, cast T2 upkeep 3 damage and Eidolon 2 self damage, then draw; hard cast costs 2R; decline under lethal Eidolon; target prompt at upkeep.

## Skewer the Critics

Source: https://scryfall.com/card/rvr/124/skewer-the-critics?utm_source=api

```json
{
  "name": "Skewer the Critics",
  "mana_cost": "{2}{R}",
  "types": [
    "Sorcery"
  ],
  "keywords": [
    "Spectacle"
  ],
  "oracle_text": "Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost life this turn.)\nSkewer the Critics deals 3 damage to any target.",
  "template": "direct_damage",
  "parameters": {
    "damage": 3,
    "targeting": "any",
    "spectacle_cost": "{R}"
  }
}
```

Tier 1, existing direct_damage, targeting any, and spectacle_cost R. Exact normal cost 2R and MV 3 independent of cost paid. Existing EffectiveCost, has_spectacle, and GoldFishRunner::DeckUsesSecondMain already handle timing and post-combat availability; ensure card has spectacle param. A hit from Chain/Rift/Magma/Eidolon may enable spectacle too; condition is opponent lost LIFE, not specifically damage/combat. Target chooser existing bucket A. Tests full cost with no lost life, R after burn/combat, second-main line, Eidolon uses MV3 in either case.

## Price of Progress

Source: https://scryfall.com/card/ema/141/price-of-progress?utm_source=api

```json
{
  "name": "Price of Progress",
  "mana_cost": "{1}{R}",
  "types": [
    "Instant"
  ],
  "keywords": [],
  "oracle_text": "Price of Progress deals damage to each player equal to twice the number of nonbasic lands that player controls.",
  "template": "direct_damage",
  "parameters": {
    "damage_per_nonbasic_land": 2
  }
}
```

Tier 2. Proposed NEW damage_per_nonbasic_land integer in CardParams/BuildParamsFromJson; direct_damage with targeting none (NOT opponent, NOT any target). Oracle has no targets and each player's amount is independent, evaluated at RESOLUTION: count that player's current lands for which !IsBasicLandCard, multiply by 2; nonbasic Mountains count, basic Snow-Covered Mountains do not. Need shared ResolveNonbasicLandDamage helper used executor ResolveDirectDamage and rollout apply_one; route through damage/replacement hooks as available, mark opponent_lost_life if appropriate. Action.direct_damage/EvalCard must estimate LIVE opponent nonbasic count, and no fixed opponent-land assumption is allowed. Opponent normally has zero lands/nonbasics in passive goldfish, so Price deals ZERO to opponent; still own-life cost with own nonbasics! No chooser (non-targeted); no target prompt. Simultaneous self+opponent lethal means draw, not a win caused by applying opponent damage before self. Test 0/0, 2 own/3 opp nonbasics ->4/6, basic snow versus nonbasic Mountain, sacrifice a Mountain with Fireblast changes later Price amount. No second-main novelty.

## Fireblast

Source: https://scryfall.com/card/dmr/119/fireblast?utm_source=api

```json
{
  "name": "Fireblast",
  "mana_cost": "{4}{R}{R}",
  "types": [
    "Instant"
  ],
  "keywords": [],
  "oracle_text": "You may sacrifice two Mountains rather than pay this spell's mana cost.\nFireblast deals 4 damage to any target.",
  "template": "direct_damage",
  "parameters": {
    "damage": 4,
    "targeting": "any",
    "alternate_sacrifice_land_count": 2,
    "alternate_sacrifice_land_subtype": "Mountain"
  }
}
```

Tier 3. Proposed NEW alternate_sacrifice_land_count 2 and alternate_sacrifice_land_subtype Mountain. This is an ALTERNATIVE cost to 4RR, never an additional cost on all casts and never replace existing Shard Volley sacrifice_land bool (one arbitrary land). Legal victims: two distinct controlled permanents with LAND type and Mountain subtype, including nonbasic Mountains; can be tapped; cannot substitute generic red-producing lands without Mountain type. Enumerate both full-mana cast and sacrifice variant as mutually exclusive actions sharing hand_index, even when both affordable. Expose all legal victim sets via provider candidates and human sacrifice chooser; dedup only proven-equivalent sets and never prune human play. Pay both sacrifices before spell resolution/on-cast triggers (cost cannot be responded to), remove by stable identity/reverse indices, fire real land-leave/death hooks; no partial payment when fewer than two. Preserve MV6 so Eidolon does NOT trigger from Fireblast even when free; prowess/Steam-Kin DO trigger since cast. Separate alt-cost state in Action/signatures/plans/replay/protocol; audit all apply paths and subset mana/land feasibility so it can spend two already-tapped Mountains without double-crediting mana, and can cast an earlier paid spell then sacrifice those lands. Existing target chooser handles any target. Hard cast requires no sacrifices. Tests two tapped Mountains free, one Mountain illegal, nonbasic typed Mountain legal, Fireblast MV6, full cost keeps lands, zero-card-count payment atomicity, Price ordering, alternative cast label human choice. No inherent second-main unlock.

## Magma Jet

Source: https://scryfall.com/card/jmp/346/magma-jet?utm_source=api

```json
{
  "name": "Magma Jet",
  "mana_cost": "{1}{R}",
  "types": [
    "Instant"
  ],
  "keywords": [
    "Scry"
  ],
  "oracle_text": "Magma Jet deals 2 damage to any target. Scry 2.",
  "template": "direct_damage",
  "parameters": {
    "damage": 2,
    "targeting": "any",
    "cast_scry": 2
  }
}
```

Tier 2 extension of direct_damage. cast_scry exists but currently runs only DrawSpell branch: EffectHandler.cpp ResolveDrawSpell ~919, TurnSolver.cpp draw path ~58740. Add post-damage ScryTop(state,2,name) to BOTH ResolveDirectDamage and apply_one direct-damage branch (~58462 cast_draw rider area). All damage first THEN scry, not scry-before-damage. If sole target becomes illegal, entire spell fails and no scry; if damage is prevented but target stays legal, scry still happens. Target and scry use existing bucket-A target/scry choosers at shared real resolution site. Ensure searched scry disposition variants/candidate plumbing includes direct_damage cast_scry, since merely adding heuristic ScryTop inside horizon violates no-greedy search rule. Audit draw/scry frontier classification currently keyed only DrawSpell/cast_draw at multiple TurnSolver sites (e.g. ~35478, ~48377, ~65995); Magma Jet seeing future draws must not disappear from search frontiers. Test damage+scry order, keep 0/1/2 and reorder, target then scry viewer prompts, executor/apply identical library and life, multiple Magma Jets consume disposition pins independently. No new second-main relevance.

## Shared integration gates

All new params: src/cards/CardDatabase.h and BuildParamsFromJson in src/cards/CardDatabase.cpp. Executor effects: src/core/EffectHandler.cpp. Rollout and action enumeration/evaluation: src/ai/TurnSolver.cpp; autonomous casting path src/ai/AIEngine.cpp when costs change. Shared hooks: src/core/SpellEffects.h. New decisions follow GameLogger.h thread-local chooser / RevealLogPause nulling, shared call site, main.cpp emitter, tools/play/index.html dispatch; update tools/play/DECISIONS.md and scripts/audit_viewer_decisions.py manifest. Existing sacrifice, target, scry decision shapes are reusable, but cast/decline suspend and Chain copy/payment need explicit modal choices. Run mechanical cost and card-field audit, mismatch harness, viewer runtime surface proof. Do not report draft Tier1 parameters as sufficient where notes specify engine gaps.
