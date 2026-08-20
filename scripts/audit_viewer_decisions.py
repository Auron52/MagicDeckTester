#!/usr/bin/env python3
"""Mechanical gate for analyze-deck Stage 5h: does the play viewer surface EVERY
interactive decision the deck's cards create?

This is the viewer analogue of `audit_card_costs.py`. A prose "classify each card's
decisions" reminder repeatedly let a card's choice ship silently heuristic-resolved;
this script removes the judgment call. It:

  1. Reads the decklist -> each card's `cards.json` params.
  2. Computes the EXPECTED set of decision `type`s from the param->type MANIFEST below
     (the machine half of tools/play/DECISIONS.md).
  3. SELF-GUARD: every choice-bearing param key present on any deck card must appear in
     the manifest -- a new interactive param added to cards.json without a manifest entry
     (and thus without viewer wiring) is a hard failure.
  4. Drives a bounded --claude-play seed sweep (the stateless-replay protocol, same as
     probe_decisions.py) and records which decision types actually SURFACED.
  5. Diffs expected vs observed and exits non-zero if any expected type never surfaced,
     or if a card carries a choice-bearing param not in the manifest.

Cards whose expected decision never got a chance to fire (the card was never cast in the
sweep) are reported as UNVERIFIED (soft) rather than failing -- forcing a rare card is the
tail 5h covers with a targeted repro. Only an expected type that the manifest predicts for
a card the sweep DID cast, yet never surfaced, is a hard MISS.

ORACLE-TEXT CROSS-CHECK (advisory, always run -- static & instant). The param manifest can
only see choices that were IMPLEMENTED as params; if the card modeling dropped a Tier 1-3
clause, a param-only audit is blind to it. So this also reads each card's real oracle text
for choice phrases ("any target", "sacrifice a creature", "search your library", "choose
one", "divided as you choose", ...) and reports any the params do NOT model -- a prompt to
read the card and wire/model or disclose it. Advisory only (regex on prose is fuzzy); it
never changes the exit code.

The <deck> may be a plain-text decklist (.txt) or a Cockatrice deck (.cod).

Usage:
  audit_viewer_decisions.py <deck> [profile] [base_seed] [n_games] [max_turns] [--no-sweep]
  audit_viewer_decisions.py <deck> <profile> [base_seed] [budget] [max_turns] --verify-card "<name>"

  --no-sweep      : static analysis only (param expectations + oracle cross-check, no binary).
                    Fast pre-check usable at implementation time, before a profile exists.
  --verify-card N : seed-search up to `budget` deterministic games biased toward casting card
                    N, then confirm its expected decision type surfaces (VERIFIED), fires-not
                    (HARD_MISS), or the card could not be forced into play (NOT_FORCED). This
                    is the automated form of 5h's targeted repro -- it closes a normal run's
                    UNVERIFIED tail for any card whose cast is forward-reachable. A decision
                    that needs a manufactured state the forward driver can't reach (e.g.
                    retrace, which needs the card already in the graveyard) reports NOT_FORCED.

Exit codes: 0 = clean (or only soft/unverified/advisory), 1 = a decision type is missing or
an unmapped choice-param was found (hard fail), 2 = usage / build error.
"""
import subprocess, sys, json, re, collections, os

BIN = os.environ.get("MTG_BIN", "./build/Release/mtg")   # override to audit a non-Release build

# ---------------------------------------------------------------------------
# MANIFEST: cards.json parameter (+ how to read it) -> decision `type` it MUST produce
# in the human-play path. Keep in lockstep with tools/play/DECISIONS.md.
#
# Each entry: json_key -> (decision_type, predicate). predicate(value) decides whether the
# param value actually creates a choice (e.g. targeting=="player" in a goldfish is not a
# choice; a scry of 0 is not a scry). A `None` predicate means "present and truthy".
# ---------------------------------------------------------------------------
def truthy(v):        return bool(v)
def positive(v):      return isinstance(v, (int, float)) and v > 0
def real_target(v):   return isinstance(v, str) and v not in ("none", "player", "")

MANIFEST = {
    "targeting":             ("target",               real_target),
    "spectacle_cost":        ("target",               truthy),   # cast post-combat, still targets
    "damage_divided":        ("divide",               truthy),
    "etb_scry":              ("scry",                 positive),
    "cast_scry":             ("scry",                 positive),
    "etb_surveil":           ("surveil",              positive),
    "cast_reorder":          ("reorder",              positive),
    "etb_dig_count":         ("dig",                  positive),
    "upkeep_adds_charge":    ("vial_charge",          truthy),
    "retrace":               ("retrace_discard",      truthy),
    "has_replicate":              ("replicate",       truthy),   # Hatchery Sliver's own replicate
    "grants_replicate_to_subtypes": ("replicate",     truthy),   # + grants it to Sliver spells
    "etb_bounce_land":       ("bounce",               truthy),
    "sacrifice_land":        ("sacrifice",            truthy),
    "expressive_iteration":  ("expressive_iteration", truthy),
    # "pay a cost, or the land enters tapped" -- shock lands (pay life) + reveal lands (Frostboil
    # Snarl, reveal a matching land). One shared `land_entry` binary modal. SURFACED, but repetitive:
    # default-OFF in the viewer options menu (let-AI-decide), per the user 2026-07-17.
    "etb_pay_life_to_untap":      ("land_entry",       positive),  # shock land: pay N life to untap
    "etb_untap_reveal_subtypes":  ("land_entry",       truthy),    # reveal land (Frostboil Snarl)
    # plan-variant sub-decisions -- surfaced inside the main_phase plan list, not their own
    # type. Verified by "does the deck offer >1 plan variant", not a distinct decision type;
    # listed here so the self-guard treats them as MAPPED (not unknown choice params).
    "tutor_to_hand":         ("main_phase",           truthy),
    "tutor_to_top":          ("main_phase",           truthy),
    # Zada/Mirrorwing solo-target trick: WHICH creature the trick targets is a plan-variant
    # sub-decision (an `enchant`-kind choose sub, one main_phase variant per legal target incl.
    # same-plan hand creatures; Action::enchant_target reused). Twinflame's strive extra-target
    # COUNT likewise (a `strive` sub on soulfire_own_targets). Not their own decision types.
    "solo_target_trick":     ("main_phase",           truthy),
    "strive_cost":           ("main_phase",           truthy),
    # Copy magnet (Zada / Mirrorwing Dragon): creates no interactive choice of its own -- the copy
    # fan-out and its resolution order are deterministic engine rules (disclosed 6a); the CHOICE
    # (whether the trick targets the magnet) lives on the trick's target sub above.
    "copies_solo_targeted_spells": ("main_phase",     truthy),
    # Dragonstorm: WHICH Dragons to put onto the battlefield -- a real multi-pick decision (the human
    # picks the subset; the engine keeps the rule's play order). Surfaces as its own `dragon` type
    # (WriteDragonDecisionJson / dragonPanelHtml). Was `main_phase` while the selection was search-only.
    "tutor_to_battlefield":  ("dragon",               truthy),
    "fetch_land_types":      ("main_phase",           truthy),
    # MDFC (Pathway) land: playing it offers a "which face?" choice surfaced as main_phase plan
    # variants (a `face` choose sub, one variant per face) -- not its own decision type.
    "mdfc_back_name":        ("main_phase",           truthy),
    # Light-Paws, Emperor's Voice: on an Aura you CAST resolving, search your library for an Aura and
    # attach it to Light-Paws. WHICH Aura is now a real human choice -- its own `lightpaws` type
    # (WriteLightPawsDecisionJson / lightPawsPanelHtml), a resolution-time chooser (g_play_lightpaws_chooser).
    # Was a heuristic-picked known gap while the fetch was engine-only.
    "aura_cast_tutor_attach": ("lightpaws",            truthy),
    # Goblin Lackey: on combat damage to a player, MAY put a Goblin permanent card from HAND onto
    # the battlefield -- WHICH card (or decline) is a real human choice -> its own `lackey_put` type
    # (WriteLackeyDecisionJson / lackeyPanelHtml), a resolution-time chooser (g_play_lackey_chooser in
    # FireCombatDamageCheatIntoPlay). Heuristic default = highest-MV matching hand card.
    "combat_damage_puts_subtype_from_hand": ("lackey_put", truthy),
    # Maelstrom Archangel: "you MAY cast a spell from your hand without paying its mana cost" on
    # combat damage. Was modelled as #FREE plan variants inside the ordinary main-phase menu, which
    # made a one-time TRIGGER behave like a standing option (castable at any moment in the phase) and
    # silently lost the charge whenever the paid variant of the same card won CheckLine's dedup. Now
    # its own `free_cast` type (WriteFreeCastDecisionJson / freeCastPanelHtml + g_play_free_cast_chooser),
    # asked once at the top of the post-combat main -- same shape as the Lackey put above.
    "combat_damage_free_cast": ("free_cast", truthy),
    # --- StompySurprise (mono-green elf ramp) ---
    # Natural Order: fetch target + sacrifice victim are plan-variant sub-decisions (one
    # main_phase variant per victim x target; Action::soulfire_own_targets reused for the victim).
    "tutor_to_battlefield_single": ("main_phase",     truthy),
    "sac_additional_creature_color": ("main_phase",   truthy),
    # Turntimber Symbiosis front: WHICH creature to put (or decline) = named plan variants.
    "look_top_put_creature_count": ("main_phase",     positive),
    # Turntimber back face: pay-3-life-or-tapped rides the shared land_entry chooser.
    "mdfc_back_pay_life":     ("land_entry",          positive),
    # Call of the Wild: activation count K = plan variants (Action::Kind::ActivateRevealTop).
    "activated_reveal_top_cost": ("main_phase",       truthy),
    # Wirewood Lodge: the untap action is a plan variant (target auto-resolved -- disclosed).
    "untap_creature_cost":    ("main_phase",          truthy),
    # Mirri's Guile: upkeep arrange-top-3 fires the shared reorder decision (ReorderNoShuffle).
    "upkeep_reorder":         ("reorder",             positive),
    # Terastodon: destroy-count K = chosen_x plan variants (WHICH Forest is fungible -- disclosed).
    "etb_destroy_own_noncreature_max": ("main_phase", positive),
    # Echo (Mogg War Marshal {1}{R}, Stingscourger {3}{R}): at upkeep, pay the echo cost OR sacrifice --
    # a real human choice -> its own `echo` type (WriteEchoDecisionJson / echoPanelHtml), an upkeep
    # chooser (g_play_echo_chooser in AIEngine echo resolution, mirroring vial_charge). Default = pay
    # if affordable (the heuristic). Binary: 1 = pay, 0 = let it die.
    "echo_cost":              ("echo",                 truthy),
    # Defense of the Heart: at upkeep (opp >= 3 creatures), sacrifice + put up to two library
    # creature cards onto the battlefield -- WHICH creatures is a real multi-pick human choice ->
    # its own `sac_tutor` type (WriteSacTutorDecisionJson / sacTutorPanelHtml), an upkeep chooser
    # (g_play_sac_tutor_chooser in PerformUpkeepSacTutor, same reply shape as `dragon`). Default =
    # the provider's SacTutorPutList (closed-form immediate-drain maximisation).
    "upkeep_sac_tutor_creatures": ("sac_tutor",        positive),
    # Crop Rotation: search a land onto the battlefield -- the target is a searched tutor-axis
    # sub-decision surfaced inside the main_phase plan variants (like tutor_to_hand/fetch), plus
    # the sacrifice_land additional cost surfaces the shared `sacrifice` decision.
    "tutor_land_to_battlefield": ("main_phase",        truthy),
    # ---- KittyEquipment (2026-08-13) ----
    # Equipment: WHICH creature to equip is a main_phase plan-variant choice (one Equip action
    # per (equipment, host) pair; human play opens every legal host via UnprunedGate::EquipHost).
    "is_equipment":          ("main_phase",           truthy),
    # Armored Skyhunter attack-dig: WHICH revealed Aura/Equipment to put reuses the `dig`
    # decision (FireAttackDigAttach -> g_play_dig_chooser); WHICH creature it attaches to is the
    # new `attach_host` type (WriteAttachHostDecisionJson / attach_host board prompt).
    "attack_dig_attach_count": ("dig",                positive),
    # Umezawa's Jitte: counter-spend at combat is the turn-keyed `jitte` type (--jitte
    # side-channel, WriteJitteDecisionJson / jittePanelHtml); the -1/-1 and lifegain modes are
    # main_phase JitteModeAbility plan actions.
    "equip_combat_damage_charges": ("jitte",          positive),
    # Balan attach-all + Stoneforge put: battlefield activations surfaced as their own
    # main_phase plan lines (AttachAllEquipment / PutFromHandAbility; attachall=/sfput= verbs).
    "attach_all_equipment_cost": ("main_phase",       truthy),
    "tap_put_from_hand_cost":    ("main_phase",       truthy),
    # Unexpectedly Absent: target rides the shared `target` decision (tuck branch consults
    # g_play_target_chooser); X rides the chosen_x plan-variant axis.
    "tuck_to_library":       ("target",               truthy),
    # Soulfire own-target selection is name/logic-driven (no param); handled by NAME_CHOICES.
}

# Cards whose interactive choice is not param-driven (matched by name).
# NB Soulfire Eruption's board-click targeting REUSES the generic `target` decision at runtime
# (main.cpp soulfire_chooser -> EnumerateTargetSets, source="Soulfire Eruption"); the distinct
# `soulfire_targets` type / WriteSoulfireDecisionJson is dead code, never emitted. So expect
# `target`, not `soulfire_targets`. (Crackle with Power is the same pattern.) Verified by live
# trace: Soulfire cast -> `target` decision, source="Soulfire Eruption", 256 (=2^8) subset opts.
NAME_CHOICES = {
    "Soulfire Eruption": "target",
}

# ---------------------------------------------------------------------------
# INERT param registry: cards.json params that create NO interactive player choice, each with
# the reason. Together with MANIFEST (decision params) + NAME_CHOICES, this must cover EVERY
# param key any deck card uses. The self-guard (below) hard-fails on a param in NEITHER set --
# so a NEW mechanic added to cards.json cannot pass until it is explicitly classified as a
# decision (mapped to a type + wired per DECISIONS.md) OR as inert (added here, with the user's
# OK). This INVERTS the old default: the previous `CHOICE_PARAM_KEYS = set(MANIFEST.keys())`
# made the guard's `elif` unreachable (any unmapped param fell through silently -- exactly how
# `has_replicate` slipped). Now every potential decision is mapped by default; an unclassified
# param stops the gate for the user's call.
#
# SAFETY BIAS: when unsure whether a param creates a choice, do NOT list it here -- leave it
# unclassified so the guard surfaces it. Over-listing (marking a real decision inert) is the
# one dangerous error; under-listing just yields a longer, safe review list.
# ---------------------------------------------------------------------------
INERT_PARAMS = {
    # stats / computed values
    "damage": "damage amount", "draw": "draw count", "cast_draw": "draw count",
    "power_bonus": "stat bonus", "tough_bonus": "stat bonus",
    "power_equals_creature_count": "computed P/T", "damage_equals_top_mv": "computed damage",
    "verse_damage": "damage detail", "x_damage_multiplier": "X-spell damage scale ({X}->main_phase)",
    "animate_power": "animated P/T", "animate_toughness": "animated P/T",
    "attack_token_power": "token P/T", "attack_token_toughness": "token P/T",
    "attack_token_subtypes": "token subtypes", "cast_token_power": "token P/T",
    "cast_token_toughness": "token P/T", "cast_token_subtypes": "token subtypes",
    "cast_trigger_subtype": "token subtype detail", "tap_token_power": "token P/T",
    "tap_token_toughness": "token P/T", "tap_token_subtypes": "token subtypes",
    "tap_token_requires_subtypes": "token gating detail", "upkeep_token_power": "token P/T",
    "upkeep_token_toughness": "token P/T", "upkeep_token_subtypes": "token subtypes",
    # automatic triggers / static effects (no choice)
    "affects_all_creatures": "board-wide static, no target",
    "attack_creates_tokens": "automatic attack trigger",
    "attack_trigger_life_loss": "automatic attack trigger",
    "cast_trigger_creates_tokens": "automatic on-cast trigger",
    "controller_lifegain_equals_power": "automatic lifegain",
    "death_trigger_damage": "automatic death trigger, no target in goldfish",
    "destroy_all_enchantments": "destroy-all, no target choice",
    "etb_opponent_lifegain": "automatic ETB", "opponent_lifegain": "automatic",
    "lifegain_to_loss": "automatic replacement", "on_cast_trigger_damage": "automatic on-cast self-damage",
    "on_cast_trigger_max_mv": "trigger threshold", "tap_opponent_lifegain": "automatic on tap",
    "tap_self_damage": "automatic on tap", "taps_spawn_opp_token": "automatic on tap (Forbidden Orchard)",
    "upkeep_creates_tokens": "automatic upkeep trigger", "landfall_damage": "damage modifier (rides `targeting`)",
    "grants_double_strike": "static grant", "grants_haste": "static grant",
    "lord_excludes_self": "lord effect detail", "affinity_for_subtype": "cost reduction",
    "hinata_cost_reducer": "cost reduction", "no_max_hand_size": "static (Reliquary Tower)",
    "max_casts_after": "spell-count restriction, no choice", "creature_mana_only": "mana-usage restriction",
    "discard_random_after_tutor": "random discard, no choice",
    # targeting/dig/tutor DETAIL params that ride an already-mapped decision
    "discount_max_targets": "targeting detail (rides `targeting`)",
    "discount_self_safe": "targeting-safety detail", "discount_targets_permanents": "targeting detail",
    "discount_targets_scale_x": "targeting/X detail", "etb_dig_requires_subtypes": "dig detail (rides etb_dig_count)",
    "etb_dig_subtypes": "dig detail (rides etb_dig_count)", "tutor_heuristic": "tutor ranking detail (rides tutor)",
    "tutor_types": "tutor detail (rides tutor)", "subtypes_affected": "lord/replicate subtype list",
    # mana production (color/source auto-resolved in the payment engine, not a surfaced choice today)
    "produces": "mana production (color auto-resolved in payment)", "produces_amount": "mana amount",
    "mdfc_back_produces": "MDFC back-face mana (colour auto-resolves once the face is picked; the face pick itself rides main_phase via mdfc_back_name)",
    "mana_rock": "mana source (color auto-resolved)", "is_filter": "mana filter (color auto-resolved)",
    "ramp_filter": "mana filter (color auto-resolved)", "reflecting": "Reflecting-Pool mana (auto-resolved)",
    "ritual_floating_mana": "ritual mana added",
    # lands: static enter-state
    "enters_tapped": "static land property", "enters_tapped_with_depletion": "static land property",
    # targeting MODIFIER -- the target choice surfaces via `targeting` (already mapped). NB the card
    # CAN target an opponent creature (real line: Invigorate + Swords to Plowshares); the goldfish
    # model picks own best attacker only because the passive opponent has no board -> a disclosed
    # (2) card-modeling limitation, NOT a viewer restriction.
    "target_own_creature": "targeting modifier; choice rides `targeting` (can also hit opponent creatures)",
    # self-declared
    "goldfish_inert": "self-declared inert marker",
    # --- Creature Giving (gift-the-opponent drain) -----------------------------------------
    # Every trigger here is automatic (no player choice): the enter-watchers are mandatory-taken
    # "you may" beneficials, the gifts are fixed-count opponent tokens, the sweep hits every
    # qualifying opponent creature, and the cumulative upkeep is a disclosed always-paid
    # auto-decision (weakly dominant vs the passive opponent; see the War-Riders bracket note).
    "etb_opp_creates_tokens": "automatic ETB gift (fixed count, single opponent -> no target)",
    "any_creature_enters_lifegain": "automatic enter trigger (Wardens)",
    "own_creature_enters_lifegain": "automatic enter trigger (Suture Priest, may-always-taken)",
    "opp_creature_enters_life_loss": "automatic enter trigger (Suture Priest, may-always-taken)",
    "etb_opp_creatures_debuff": "automatic ETB sweep, hits every qualifying opponent creature",
    "opp_dies_life_loss": "automatic death trigger",
    "cumulative_upkeep_opp_token": "always-paid auto-decision (disclosed; weakly dominant in goldfish)",
    "upkeep_sac_tutor_opp_min": "trigger threshold (rides upkeep_sac_tutor_creatures -> sac_tutor)",
    # --- Dragonstorm (mono-red ritual/storm combo) -----------------------------------------
    # Token creation is automatic (no choice); its P/T/subtypes are computed detail.
    "attack_per_matching_creates_tokens": "automatic attack trigger (Utvara: per-attacking-Dragon token)",
    "attack_per_token_power": "token P/T", "attack_per_token_toughness": "token P/T",
    "attack_per_token_subtypes": "token subtypes",
    "attack_token_requires_subtypes": "token gating detail (which attackers make a token)",
    "etb_other_subtype_creates_tokens": "automatic ETB trigger (Lathliss: per other nontoken Dragon)",
    "etb_created_token_power": "token P/T", "etb_created_token_toughness": "token P/T",
    "etb_created_token_subtypes": "token subtypes", "etb_token_requires_subtype": "ETB token gating detail",
    # automatic triggers, no choice
    "tutor_shuffle_after": "automatic shuffle after tutor, no choice",
    "impulse_exile": "automatic exile of top N (Apex: exile top 7); which exiled cards to CAST rides main_phase",
    "impulse_expiry_this_turn": "automatic end-of-turn expiry of staged exile, no choice",
    "ritual_float_gy_self_bonus": "automatic graveyard-count float scaling (Rite of Flame), no choice",
    # mana production -- color/amount auto-resolved by the payment engine (user: 'mana we leave to the engine')
    "impulse_float_amount": "impulse mana float amount; color auto-resolved in payment (left to engine)",
    "sac_for_mana_amount": "Lotus Bloom sac-for-mana amount; color auto-resolved (left to engine); when-to-sac rides the search plan",
    "ritual_float_color": "ritual float color (fixed R), no choice",
    "storage_land": "storage battery: charge auto-while-idle; burst amount search-resolved via payment (like other mana sources)",
    "storage_charge_mode": "storage charge timing (auto-while-idle), no choice",
    # static cost reducer
    "reduces_spell_color": "static per-copy color cost reducer (Ruby Medallion), no choice",
    # pump-amount detail params that ride the DEFERRED firebreathing/team_pump decision
    "firebreathing_power": "pump power-per-{R} detail (rides firebreathing_cost, DEFERRED)",
    "team_pump_power": "team pump power detail (rides team_pump_cost, DEFERRED)",
    "team_pump_subtypes": "team pump subtype filter detail (rides team_pump_cost, DEFERRED)",
    # --- Auras (Bogles): effect/stat/trigger detail params -- no interactive player choice ---
    "aura_power_bonus": "aura stat grant (rides is_aura)", "aura_tough_bonus": "aura stat grant (rides is_aura)",
    "aura_grants_lifelink": "automatic lifelink grant, no choice",
    "aura_scale_kind": "aura scaling selector (rides is_aura)", "aura_scale_power": "aura scaling stat (rides is_aura)",
    "aura_scale_tough": "aura scaling stat (rides is_aura)",
    "aura_enchant_requires": "aura enchant-target restriction (narrows the is_aura main_phase variants)",
    "aura_self_buff_power": "Kor static per-aura self-buff, computed P/T", "aura_self_buff_tough": "Kor static per-aura self-buff, computed P/T",
    "draw_on_aura_cast": "Kor may-draw auto-resolved as always-draw (strictly good in goldfish), no meaningful choice",
    "fastland_max_other_lands": "static land property (conditional enters-tapped, Razorverge)",
    # --- Goblins: automatic triggers / static effects / computed detail (NO player choice) -----
    # Combat/attack triggers -- automatic, applied at declare-attackers, no choice:
    "attack_pump_power_per_other_matching": "automatic attack trigger (Piledriver +2/+0 per other attacking Goblin)",
    "attack_self_pump_per_other_subtype": "automatic attack trigger (Muxus +1/+1 per other Goblin)",
    "attack_self_pump_power": "attack-trigger pump amount detail", "attack_self_pump_tough": "attack-trigger pump amount detail",
    # ETB triggers -- automatic, no choice (damage targets face in goldfish; reveal puts ALL matching):
    "etb_self_creates_tokens": "automatic ETB token creation (Mogg 1, Siege-Gang 3), no choice",
    "etb_damage_any": "automatic ETB damage, no target in goldfish (face)",
    "etb_damage_each_opponent": "automatic ETB AoE (each opponent + their creatures), no choice",
    "etb_reveal_count": "automatic ETB reveal (Muxus reveal-6, put ALL matching Goblins MV<=5), no choice",
    "etb_reveal_put_creatures_only": "Muxus reveal gating detail (rides etb_reveal_count)",
    "etb_reveal_put_max_mv": "Muxus reveal MV cap detail (rides etb_reveal_count)",
    "etb_reveal_put_subtypes": "Muxus reveal subtype filter detail (rides etb_reveal_count)",
    # Death-watch triggers -- automatic on a Goblin dying, no choice (damage to face; impulse auto):
    "dies_watch_subtype": "death-watch subtype gating detail", "dies_watch_includes_self": "death-watch self-inclusion detail",
    "dies_trigger_damage": "automatic death trigger, no target in goldfish (Pashalik ping -> face)",
    "dies_trigger_creates_tokens": "automatic death trigger (Mogg death token), no choice",
    "dies_token_power": "death token P/T detail", "dies_token_toughness": "death token P/T detail", "dies_token_subtypes": "death token subtypes",
    "dies_trigger_impulse_exile": "automatic death trigger (Rundvelt impulse-exile top card); which exiled card to CAST rides main_phase",
    "dies_impulse_requires_subtype": "death-impulse castability gating detail (rides dies_trigger_impulse_exile)",
    "dies_impulse_expiry_next_turn": "death-impulse expiry-window detail (rides dies_trigger_impulse_exile)",
    # Sac-outlet EFFECT detail -- the ACTIVATION rides main_phase (sac_creature_outlet); these are its payload/cost:
    "sac_creature_cost": "sac-outlet activation cost detail (rides sac_creature_outlet main_phase)",
    "sac_creature_requires_subtype": "sac-outlet victim gating detail (which subtype; rides sac_creature_outlet)",
    "sac_outlet_add_mana_color": "sac-outlet mana output color (Skirk {R}); auto-resolved (left to engine)",
    "sac_outlet_add_mana_amount": "sac-outlet mana output amount; auto-resolved (left to engine)",
    "sac_outlet_damage": "automatic sac-outlet damage, no target in goldfish (Siege-Gang -> face)",
    "sac_outlet_creates_tokens": "automatic sac-outlet token creation (Pashalik 2), no choice",
    "sac_outlet_token_power": "sac-outlet token P/T detail", "sac_outlet_token_toughness": "sac-outlet token P/T detail",
    "sac_outlet_token_subtypes": "sac-outlet token subtypes detail",
    # Krenko token detail -- the {T} activation rides main_phase (tap_creates_tokens_per_controlled_subtype):
    "tap_created_token_power": "Krenko token P/T detail", "tap_created_token_toughness": "Krenko token P/T detail",
    "tap_created_token_subtypes": "Krenko token subtypes detail",
    # Channel damage payload -- the {1}{R}-discard ACTIVATION rides main_phase (channel_cost); damage is to face:
    "channel_damage": "automatic Channel damage payload, no target in goldfish (Twinshot -> face)",
    # Three Tree City board-scaled mana -- a mana SOURCE; color/amount auto-resolved by the payment engine:
    "mana_per_creature_subtype": "Three Tree {2},{T} board-scaled mana source; color/amount auto-resolved (left to engine)",
    "mana_per_creature_feeder_generic": "Three Tree scaled-mana activation feeder cost detail (rides mana_per_creature_subtype)",
    # Static cost reducer / mana restriction -- no choice:
    "reduces_spell_subtype": "static Goblin-spell cost reducer (Warchief, {1} less), no choice",
    "colored_creature_only": "Cavern of Souls mana restriction (spend only on creature spells), no choice",
    # --- FiveColour: automatic triggers / mana production / detail params (NO player choice) ---
    # (Choice-bearing FiveColour params are in MAINPHASE_PARAMS below; the underlying resolution
    # decisions -- Archangel banking, Unite mode collapse, Deathrite fungible-fuel picks, the
    # planeswalker/Garth heuristic sub-picks -- were user-approved 2026-08-06, disclosed in 6a.)
    "attack_draw_cards": "automatic attack trigger (Two-Headed Hellkite: draw on attack), no choice",
    "colored_cast_lifegain": "automatic on-cast lifegain (Ancient Cornucopia, once per turn), no choice",
    "multicolor_cast_damage_per_color": "automatic on-cast trigger (Mana Cannons); 'any target' -> face in goldfish (disclosed)",
    "domain_mana": "domain-scaled mana production (Faeburrow/Bloom Tender); color/amount auto-resolved in payment (left to engine)",
    "domain_self_pump": "computed P/T (Faeburrow Elder +1/+1 per color among permanents), no choice",
    "equip_cost_generic": "equip cost detail (rides is_equipment)",
    "equip_grants_haste": "static grant detail (rides is_equipment)",
    "equip_grants_shroud": "static grant detail (goldfish-inert; rides is_equipment)",
    "graveyard_replace_shuffle_library": "automatic replacement effect (Progenitus shuffle-in), no choice",
    # --- StompySurprise (mono-green elf ramp) ---
    "etb_life_floor": "automatic ETB life set (Elderscale Wurm), no choice",
    "mana_per_creature_count_all": "scaled-dork count scope detail (rides mana_per_creature_subtype)",
    "mana_requires_land_subtype": "conditional mana gate (Arbor Elf); WHICH Forest untapped is fungible -- disclosed",
    "etb_team_pump_per_creature": "automatic ETB team pump (Craterhoof), no choice",
    "creature_enters_min_power": "enter-watcher power filter detail (Vaultborn)",
    "own_creature_enters_draw": "automatic enter-trigger draw rider (may-always-taken)",
    "creature_enters_includes_self": "enter-watcher self-inclusion detail",
    "dies_trigger_copy_self_token": "automatic death trigger (Vaultborn copy), no choice",
    "created_token_color": "token colour detail",
    "look_put_counter_bonus": "counter-bonus detail (rides look_top_put_creature_count)",
    "look_put_counter_bonus_max_mv": "counter-bonus threshold detail",
    "untap_creature_subtype": "untap-target subtype detail (rides untap_creature_cost)",
    "tutor_color": "tutor colour filter detail (rides the tutor target axis)",
    "loyalty_start": "starting loyalty stat, no choice",
    "modal_damage_per_choice": "modal payload detail (rides modal_choose_n)",
    "modal_draw_per_choice": "modal payload detail (rides modal_choose_n)",
    # Zada/Mirrorwing trick payload details (the CHOICE -- which creature / strive count -- rides
    # solo_target_trick / strive_cost, both mapped to main_phase plan variants above):
    "trick_up_to_one": "trick payload detail (adds the untargeted variant; rides solo_target_trick)",
    "pump_per_cards_drawn_power": "computed pump (rides solo_target_trick)",
    "gy_self_power_bonus": "computed pump (rides solo_target_trick)",
    "pump_per_treasure_power": "computed pump (rides solo_target_trick)",
    "pump_per_treasure_tough": "computed pump (rides solo_target_trick)",
    "creates_treasures": "automatic token creation (Treasure; sac-for-mana is a searched plan action)",
    "grants_temp_haste": "automatic until-EOT grant to the chosen target (rides solo_target_trick)",
    "counters_on_target": "automatic counter on the chosen target (rides solo_target_trick)",
    "cast_lifegain": "automatic lifegain",
    "grants_extra_land_drop": "automatic bonus land drop (the drop itself is the normal land choice)",
    "token_copy_of_target": "automatic token copy of the chosen target (rides solo_target_trick)",
    "etb_lifegain": "automatic land ETB lifegain (Kazandu Refuge)",
    "checkland_subtypes": "static land entry condition (Rootbound Crag), no choice",
    # --- KittyEquipment: equipment rider/legality/trigger detail params (NO new player choice) ---
    # The choice-bearing equipment params are in MANIFEST above: is_equipment (host pick ->
    # main_phase equip variants + attach_host), attack_dig_attach_count (-> dig + attach_host),
    # equip_combat_damage_charges (-> jitte), attach_all_equipment_cost / tap_put_from_hand_cost
    # (-> main_phase), tuck_to_library (-> target). Everything below is payload/legality detail
    # riding one of those, or an automatic trigger.
    "equip_power_bonus": "equipment stat grant (rides is_equipment)",
    "equip_tough_bonus": "equipment stat grant (rides is_equipment)",
    "equip_grants_lifelink": "automatic lifelink grant while equipped, no choice",
    "equip_min_power": "equip legality gate (O-Naginata power>=3); narrows is_equipment host variants, no extra choice",
    "equip_sacrifices_prior_host": "mandatory unattach-sac trigger (Grafted Wargear), no choice",
    "metalcraft_equip_zero_artifacts": "static conditional cost modifier (Puresteel metalcraft), no choice",
    "double_strike_while_equipped": "static conditional keyword grant (Kor Duelist), no choice",
    "double_strike_min_equipment": "static conditional keyword grant threshold (Balan), no choice",
    "draw_on_equipment_etb": "Puresteel may-draw auto-resolved as always-draw (strictly good; disclosed in 6a), no meaningful choice",
    "upkeep_tokens_per_equipment": "mandatory upkeep trigger (Kemba cat per Equipment), token count computed, no choice",
    "charge_pump_power": "Jitte mode payload detail (mode pick surfaces as main_phase JitteModeAbility; combat spend rides equip_combat_damage_charges -> jitte)",
    "charge_pump_tough": "Jitte mode payload detail (rides equip_combat_damage_charges)",
    "charge_minus_power": "Jitte -1/-1 mode payload detail (the creature pick surfaces as per-target main_phase JitteModeAbility variants)",
    "charge_minus_tough": "Jitte -1/-1 mode payload detail (rides equip_combat_damage_charges)",
    "charge_lifegain": "Jitte lifegain mode payload detail (mode pick surfaces as main_phase JitteModeAbility)",
    "allow_self_target": "targeting-legality broadening detail (Unexpectedly Absent; the pick itself rides tuck_to_library -> target)",
    "tap_put_from_hand_types": "put-from-hand card-type filter detail (rides tap_put_from_hand_cost -> main_phase)",
}

# Decisions the human makes by picking among main_phase PLAN VARIANTS or a board-click
# activation -- surfaced, but not as a distinct verifiable decision `type`. Classified as
# MAPPED (they pass the guard); the per-deck obligation is that every legal variant/activation
# is OFFERED (unpruned) in the plan list -- verified in the sweep, not by a distinct type.
MAINPHASE_PARAMS = {
    "discard_land_damage": "Land's Edge discard-a-land activation (board-click source, main.cpp:100)",
    "cycling_cost":        "cycling = discard-to-draw activation from hand (main_phase play)",
    "sacrifice_draw_cost": "Fiery Islet sac-to-draw activation (main_phase play)",
    "stages_cards":        "Light Up the Stage: staged cards become castable (main_phase plays)",
    "tap_token_cost":      "Sliver Hive activated token ability (main_phase play)",
    "alt_cost_requires_subtype": "free-pitch alt cost = a distinct cast plan variant",
    "alt_lifegain_cost":   "free-pitch alt cost (opponent-lifegain half is goldfish-inert)",
    "animate_cost":        "animate = main_phase activation",
    "can_animate":         "capability flag; animate rides main_phase",
    "splice_onto_arcane":  "Desperate Ritual splice count = a main_phase plan variant (emitted in main.cpp)",
    "suspend_time_counters": "Lotus Bloom suspend = a {0} main_phase action; when-to-suspend is a plan variant",
    "is_aura":             "Aura enchant-target = a main_phase plan variant (one per legal creature, offered unpruned; SummarizePlan labels it, main.cpp emits enchant_target)",
    # --- Goblins: activated abilities the search enumerates as top-level plan actions ---------
    "sac_creature_outlet": "sac-a-Goblin outlet activation (Skirk {R}/Siege-Gang {1}{R}/Pashalik {3}{R}): the ACTIVATION rides main_phase via the sacout= line verb (LineSpec::sac_outlets), and WHICH victim dies is the `sacrifice` board decision at resolution (ChooseSacOutletVictimIndex) -- no longer a disclosed heuristic gap (2026-08-08)",
    "channel_cost":        "Twinshot Sniper Channel = a from-HAND {1}{R}-discard activation = a main_phase plan action (Channel action)",
    "tap_creates_tokens_per_controlled_subtype": "Krenko {T}: make X Goblins = a main_phase tap activation (TapForTokens action)",
    # --- FiveColour: activations / choices the search enumerates as main_phase plan variants ---
    "gy_land_exile_mana": "Deathrite land-exile mana activation = a main_phase plan action (DRE#); which land = fungible-fuel heuristic sub-choice (disclosed 6a)",
    "gy_exile_instant_sorcery_drain": "Deathrite instant/sorcery-exile drain activation = a main_phase plan action (DRE#); which card = fungible (disclosed 6a)",
    "gy_exile_creature_lifegain": "Deathrite creature-exile lifegain activation = a main_phase plan action (DRE#); which card = fungible (disclosed 6a)",
    "is_equipment": "Equip = a main_phase plan action (EQ#), one variant per legal creature target",
    "loyalty_abilities": "planeswalker loyalty activation = a main_phase plan action (PW#), one variant per affordable ability; ability sub-targets heuristic-resolved (disclosed 6a)",
    "garth_copy_ability": "Garth One-Eye conjure-cast = a main_phase plan action (GARTH#), one variant per un-used name; conjured-card sub-targets heuristic-resolved (disclosed 6a)",
    "modal_choose_n": "Unite the Coalition: S in [0..5] mode-count split = main_phase plan variants (#S); mode collapse user-approved 2026-08-06 (disclosed 6a)",
}

# DEFAULT after onboarding = SURFACE every decision (user 2026-07-17). "Let the AI decide" is a
# per-decision USER opt-in in the viewer's options menu for convenience (e.g. shocklands, where
# constant prompting gets annoying). It lives in the VIEWER (localStorage-backed), not here: the
# engine still ALWAYS emits every decision, so surfacing is never silently skipped at the engine
# level. The two land-entry choices (shockland pay-life `etb_pay_life_to_untap`, Snarl reveal
# `etb_untap_reveal_subtypes`) are now MAPPED to the `land_entry` type (wired per DECISIONS.md);
# they ship default-OFF in that menu (the heuristic pays/reveals only when it benefits you). And
# targets are NEVER restricted in human-play: the target dialog offers every legal target (own AND
# opponent), per the "provider must not narrow in human-play" invariant -- a truncated target list
# is a surfacing bug, not a heuristic.

# Known unwired decision gaps, DEFERRED with the user's sign-off (disclosed in Stage 6a).
DEFERRED_PARAMS = {
    "cascade_max_mv":       "cascade SEARCH target -- heuristic-picked (DECISIONS.md known gap)",
    "untap_x_mana_sources": "Reality Spasm untap mode -- needs an engine-model change (phase-2 gap)",
    # Dragonstorm: pump + ping are real player choices but currently search-resolved. User (2026-07-19)
    # signed off on deferring the WIRING to the planned viewer options-menu toggle system ("every choice
    # toggleable... can be done in the future"), with these defaults:
    "firebreathing_cost":   "Scourge firebreathing pump amount -- currently search-resolved w/ leftover "
                            "combat mana; user wants it a toggleable choice, ON by default (deferred to the "
                            "viewer options-menu toggle system)",
    "team_pump_cost":       "Lathliss team pump amount -- currently search-resolved; user wants it a "
                            "toggleable choice, ON by default (deferred to the options-menu toggle system)",
    "dragon_ping_on_enter": "Scourge ETB ping target -- any-target collapses to face in the goldfish; user "
                            "wants a toggleable target choice, OFF by default (deferred to the options-menu "
                            "toggle system; matters more in phase-2 with a real opponent)",
}

DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")


def load_deck_cards(deck_path, cards_json="src/cards/data/cards.json"):
    names = set()
    if deck_path.lower().endswith(".cod"):
        # Cockatrice XML: <card number="4" name="..."/> under the "main" zone.
        import xml.etree.ElementTree as ET
        root = ET.parse(deck_path).getroot()
        for zone in root.iter("zone"):
            if zone.get("name") != "main":
                continue
            for card in zone.iter("card"):
                nm = card.get("name")
                if nm:
                    names.add(nm.strip())
    else:
        for ln in open(deck_path):
            ln = ln.strip()
            if not ln or ln.lower() in ("sideboard", "mainboard"):
                continue
            names.add(re.sub(r"^\s*\d+x?\s+", "", ln).strip())
    d = json.load(open(cards_json))
    cards = d if isinstance(d, list) else list(d.get("cards", d.values()))
    return [c for c in cards if c.get("name") in names], names


def expected_for_card(card):
    """(set of expected decision types, set of unmapped choice-param keys) for one card."""
    p = card.get("parameters", {}) or {}
    exp, unmapped = set(), set()
    for key, val in p.items():
        if key in MANIFEST:
            dtype, pred = MANIFEST[key]
            if pred(val):
                exp.add(dtype)
        elif key in INERT_PARAMS or key in MAINPHASE_PARAMS or key in DEFERRED_PARAMS:
            pass  # classified: inert / main_phase-ride / user-approved deferral
        else:
            unmapped.add(key)   # UNCLASSIFIED -> unmapped decision (wire it to surface) or new param -> guard trips
    # X spells: {X} in the mana cost -> chosen_x plan variant (rides main_phase)
    if "{X}" in (card.get("mana_cost") or ""):
        exp.add("main_phase")
    if card.get("name") in NAME_CHOICES:
        exp.add(NAME_CHOICES[card["name"]])
    return exp, unmapped


# ---------------------------------------------------------------------------
# ORACLE-TEXT CROSS-CHECK (advisory). The param manifest above can only see choices that
# were IMPLEMENTED as params -- if the card modeling dropped a Tier 1-3 clause (the exact
# "hand-waved the gist" failure analyze-deck warns about), a param-only audit is blind to
# it. So also read the real oracle text for choice-creating phrases and diff what the TEXT
# implies against what the PARAMS model. A gap is advisory (regex on prose is fuzzy), and
# means one of: a genuinely dropped/unmodeled choice (-> Stage 2), a goldfish-inert clause
# (-> disclose; usually carries a bracket note), or a regex false match (-> ignore).
# ---------------------------------------------------------------------------
ORACLE_PATTERNS = [
    ("target",    re.compile(r"\bany target\b|\btarget (creature|permanent|artifact|"
                             r"enchantment|nonland permanent|creature or planeswalker)\b", re.I)),
    ("sacrifice", re.compile(r"\bsacrifice (a|an|another) (creature|land|artifact|"
                             r"permanent|enchantment|nonland permanent)\b", re.I)),
    ("search",    re.compile(r"\bsearch your library\b", re.I)),
    ("scry",      re.compile(r"\bscry \d", re.I)),
    ("surveil",   re.compile(r"\bsurveil \d", re.I)),
    ("modal",     re.compile(r"\bchoose (one|two|three|one or more|up to)\b", re.I)),
    ("divide",    re.compile(r"\bdivided (as you choose|among|evenly)\b", re.I)),
    ("bounce",    re.compile(r"\breturn .{0,40}\bland\b.{0,25}to (its|their) owner'?s? hand", re.I)),
    ("discard",   re.compile(r"\bdiscard (a|one|two|three|\d+) .{0,20}?card(?!.{0,12}at random)", re.I)),
]
INERT_NOTE = re.compile(r"\[[^\]]*(inert|deferred|not modelled|not modeled|resolved by|"
                        r"goldfish|simplified)[^\]]*\]", re.I)


def modeled_tokens(card):
    """The choice tokens the card's PARAMS actually surface a decision for."""
    p = card.get("parameters", {}) or {}
    t = set()
    if real_target(p.get("targeting", "none")) or p.get("spectacle_cost"): t.add("target")
    if p.get("sacrifice_land"):                                            t.add("sacrifice")
    if p.get("etb_scry", 0) > 0 or p.get("cast_scry", 0) > 0:              t.add("scry")
    if p.get("etb_surveil", 0) > 0:                                        t.add("surveil")
    if p.get("damage_divided"):                                           t.add("divide")
    if p.get("etb_bounce_land"):                                          t.add("bounce")
    if p.get("retrace") or p.get("discard_land_damage"):                  t.add("discard")
    if p.get("tutor_to_hand") or p.get("tutor_to_top") or p.get("fetch_land_types") \
            or p.get("tutor_to_battlefield_single") or p.get("tutor_land_to_battlefield") \
            or p.get("look_top_put_creature_count", 0) > 0:
        t.add("search")
    # Natural Order's "sacrifice a green creature" additional cost is a modeled plan-variant
    # choice (sac victim variants), so its "sacrifice a/an" phrase is covered.
    if p.get("sac_additional_creature_color"):
        t.add("sacrifice")
    return t


def oracle_advisories(card):
    """[(token, snippet, has_inert_note)] for choice phrases in the text NOT modeled by params."""
    text = (card.get("oracle_text") or "")
    if not text:
        return []
    modeled = modeled_tokens(card)
    has_note = bool(INERT_NOTE.search(text))
    # Scan the ORACLE text with implementer bracket-notes stripped -- those are comments, not
    # card text, and their prose causes false matches (e.g. a note mentioning "Scry 3").
    scan = re.sub(r"\[[^\]]*\]", "", text)
    out = []
    for token, rx in ORACLE_PATTERNS:
        m = rx.search(scan)
        if m and token not in modeled:
            s = m.start()
            snippet = scan[max(0, s - 10):s + 40].replace("\n", " ").strip()
            out.append((token, snippet, has_note))
    return out


def step(deck, prof, seed, gi, choices, max_turns, jitte=None):
    # Game seed = base + gi: the engine uses --seed VERBATIM as the game's shuffle seed
    # (--game-index only sets numbering/labels -- same convention as the single-game repro
    # recipe). Passing a constant base seed here made every "game" of the sweep the SAME
    # shuffle replayed n_games times, which silently gutted coverage and made every
    # verify-card "seed-search" search one game.
    #
    # --jitte-prompt: surface the turn-keyed jitte side-channel decision (Umezawa's Jitte
    # counter spend) exactly as the play viewer does (server.js always passes it). Without it
    # the engine silently replays the greedy default -- which reads as a HARD MISS here.
    # Harmless on decks without a Jitte (the chooser never fires). Replies accumulate in
    # `jitte` as "turn:count" pairs, NOT in the positional --choices stream.
    cmd = [BIN, deck, "--profile", prof, "--claude-play", "--seed", str(seed + gi),
           "--game-index", str(gi), "--max-turns", str(max_turns),
           "--reveal", "6", "--choices", ",".join(map(str, choices)), "--jitte-prompt"]
    if jitte:
        cmd += ["--jitte", ",".join(jitte)]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    if RES_RE.search(out):
        return None
    m = DEC_RE.search(out)
    return json.loads(m.group(1)) if m else None


def pick(d):
    """Drive a *developing* line so as many cards as possible get cast."""
    t = d.get("type")
    # mulligan/bottom use ai_choice (NOT heuristic_default); defaulting to 0 on a mulligan
    # means "mulligan again" -> an infinite mulligan loop that never reaches a turn.
    if t == "mulligan":
        ac = d.get("ai_choice")
        return ac if isinstance(ac, int) else 1          # 1 = keep, so we make progress
    if t == "bottom":
        ac = d.get("ai_choice")
        if isinstance(ac, dict):
            return ac.get("index", 0)
        return ac if isinstance(ac, int) else 0
    if t == "main_phase":
        plans = d.get("plans", [])
        if not plans:
            return -1
        # Prefer a plan that casts something (longest summary ~ most actions).
        return max(plans, key=lambda p: len(p.get("summary", "")))["index"]
    if t == "dragon":
        # Dragonstorm put override: a MULTI-int reply (one 0/1 flag per candidate). Follow the AI
        # default (ai_set) -- return a list; the driver's append helper extends the choices stream.
        ai = set(d.get("ai_set") or [])
        return [1 if c.get("index") in ai else 0 for c in (d.get("candidates") or [])]
    if "heuristic_default" in d:
        return d["heuristic_default"]
    return 0


def push_choice(choices, choice):
    """Append a decision's reply to the --choices stream. Most decisions reply a single int; the
    multi-int ones (dragon put override) reply a LIST of ints, extended positionally."""
    if isinstance(choice, list):
        choices.extend(choice)
    else:
        choices.append(choice)


def pick_toward(d, target_lc):
    """Like pick(), but on a main_phase choice PREFER a plan that casts the target card, so a
    seed-search can force a specific card into play to verify its decision surfaces."""
    if d.get("type") == "main_phase":
        plans = d.get("plans", [])
        if not plans:
            return -1
        want = [p for p in plans if target_lc in p.get("summary", "").lower()]
        pool = want or plans
        return max(pool, key=lambda p: len(p.get("summary", "")))["index"]
    return pick(d)


def verify_card(deck, prof, card_name, expected_types, base_seed, budget, max_turns):
    """Seed-search for a game that casts `card_name`, then confirm its expected decision
    type(s) surface. Returns (status, detail): VERIFIED / HARD_MISS / NOT_FORCED.

    Closes the auditor's UNVERIFIED tail without an engine change: instead of hoping a card
    is drawn in the fixed sweep, drive many deterministic games biased toward casting it.
    Type-level attribution (a decision of the expected type appeared in a game where the card
    was cast) -- unambiguous unless the deck has two cards producing the SAME type.
    """
    target_lc = card_name.lower()
    cast_seen_anywhere = False
    drawn_games = 0             # games where the card was ever in hand (distinguishes NOT_FORCED reasons)
    for gi in range(budget):
        choices, jitte, guard = [], [], 0
        observed = set()            # (type, source_lc)
        cast_here = False
        in_hand_here = False
        seen_states = set()         # repeat-state pass rule; see run_sweep
        while guard < 220:
            guard += 1
            d = step(deck, prof, base_seed, gi, choices, max_turns, jitte)
            if d is None:
                break
            t = d.get("type")
            if t not in ("main_phase", "mulligan", "bottom", "?"):
                observed.add((t, (d.get("source") or "").lower()))
            hand = (d.get("me", {}) or {}).get("hand") or []
            if any(str(c.get("name", "")).lower() == target_lc for c in hand):
                in_hand_here = True
            choice = pick_toward(d, target_lc)
            if t == "jitte":                 # side-channel reply, keyed by turn (see step())
                jitte.append(f"{d.get('turn')}:{choice}")
                continue
            if t == "main_phase":
                key = (d.get("turn"),
                       tuple(sorted(p.get("summary", "") for p in d.get("plans", []))))
                if key in seen_states:
                    choice = -1
                else:
                    seen_states.add(key)
            if t == "main_phase" and isinstance(choice, int) and choice >= 0:
                for pl in d.get("plans", []):
                    if pl.get("index") == choice and target_lc in pl.get("summary", "").lower():
                        cast_here = True
                        break
            push_choice(choices, choice)
        if in_hand_here:
            drawn_games += 1
        if cast_here:
            cast_seen_anywhere = True
            hit = {t for (t, _) in observed if t in expected_types}
            if hit:
                src_confirmed = any(t in expected_types and target_lc in s for (t, s) in observed)
                return ("VERIFIED", {"seed": base_seed, "game_index": gi,
                                     "types": sorted(hit), "source_confirmed": src_confirmed})
    if cast_seen_anywhere:
        return ("HARD_MISS", {"note": "card was cast but no expected decision surfaced"})
    if drawn_games:
        return ("NOT_FORCED", {"note": f"card was in hand in {drawn_games}/{budget} games but never "
                               f"cast (never castable/offered on the driven line -- try a hand-built "
                               f"--choices line, more turns, or a wider seed)"})
    return ("NOT_FORCED", {"note": f"card never drawn in {budget} games from seed {base_seed} "
                           f"(try a different/wider seed or more games)"})


def run_sweep(deck, prof, base_seed, n_games, max_turns):
    """Return (observed decision types, text of the plans actually CHOSEN).

    Only the chosen plan's summary counts as "cast" -- scanning every offered variant would
    mark a card merely OFFERED as cast and produce false hard-misses.
    """
    GUARD = 160
    observed = collections.Counter()
    cast_text = []
    stuck = 0                       # games that hit the guard without finishing (driver pathology)
    for gi in range(n_games):
        choices, jitte, guard = [], [], 0
        seen_states = set()         # (turn, plan-summaries) already offered this game -- see below
        while guard < GUARD:
            guard += 1
            d = step(deck, prof, base_seed, gi, choices, max_turns, jitte)
            if d is None:
                break
            observed[d.get("type", "?")] += 1
            choice = pick(d)
            if d.get("type") == "jitte":     # side-channel reply, keyed by turn (see step())
                jitte.append(f"{d.get('turn')}:{choice}")
                continue
            # Free repeatable actions (equip {0}: Lightning Greaves, metalcraft) legally recur
            # forever -- the engine faithfully re-offers "move the equipment back" after every
            # apply, so a driver that always takes a plan toggles it until the guard trips.
            # A real player passes; do the same when this exact decision state has already
            # been offered this game (progress always changes the plan list).
            if d.get("type") == "main_phase":
                key = (d.get("turn"),
                       tuple(sorted(p.get("summary", "") for p in d.get("plans", []))))
                if key in seen_states:
                    choice = -1
                else:
                    seen_states.add(key)
            if d.get("type") == "main_phase" and isinstance(choice, int) and choice >= 0:
                for pl in d.get("plans", []):
                    if pl.get("index") == choice:
                        cast_text.append(pl.get("summary", "").lower())
                        break
            push_choice(choices, choice)
        if guard >= GUARD:
            stuck += 1
    return observed, cast_text, stuck


def print_oracle_crosscheck(cards):
    """Advisory: what the oracle TEXT implies vs what the params model. Never fails the
    build (fuzzy), but every line is a card whose text mentions a choice the modeling does
    not surface -- triage each: real drop -> Stage 2; inert -> disclose; false match -> ignore."""
    findings = []
    for c in cards:
        for token, snippet, note in oracle_advisories(c):
            findings.append((c["name"], token, snippet, note))
    print("\n--- ORACLE-TEXT CROSS-CHECK (advisory) ---")
    if not findings:
        print("  No oracle-text choice phrase is left unmodeled by params. Clean.")
        return
    print("  Card text mentions a choice the params do NOT model a decision for. Triage each\n"
          "  (real dropped choice -> Stage 2/2c-ter; goldfish-inert -> disclose; regex false\n"
          "  match -> ignore). '[note]' = the card carries a disclosed inert/deferred note.")
    for name, token, snippet, note in findings:
        tag = "  [has inert/deferred note]" if note else ""
        print(f"  {name}: text implies '{token}'  (...{snippet}...){tag}")


def main():
    raw = sys.argv[1:]
    no_sweep = "--no-sweep" in raw
    verify_name = None
    if "--verify-card" in raw:
        i = raw.index("--verify-card")
        verify_name = raw[i + 1] if i + 1 < len(raw) else None
        raw = raw[:i] + raw[i + 2:]
    argv = [a for a in raw if not a.startswith("--")]
    if len(argv) < 1:
        print(__doc__)
        return 2
    deck = argv[0]
    prof = argv[1] if len(argv) > 1 else None
    base_seed = int(argv[2]) if len(argv) > 2 else 9001
    n_games   = int(argv[3]) if len(argv) > 3 else 40
    max_turns = int(argv[4]) if len(argv) > 4 else 12

    # --verify-card mode: seed-search to confirm one card's decision surfaces (targeted repro).
    if verify_name:
        if prof is None:
            print("ERROR: --verify-card needs a profile path.")
            return 2
        cards, _ = load_deck_cards(deck)
        match = next((c for c in cards if c["name"].lower() == verify_name.lower()), None)
        if match is None:
            print(f"ERROR: '{verify_name}' not found in {deck}.")
            return 2
        exp, _ = expected_for_card(match)
        exp.discard("main_phase")
        if not exp:
            print(f"{verify_name}: no param-driven interactive decision expected. Nothing to verify.")
            return 0
        print(f"Verifying '{verify_name}' surfaces {sorted(exp)} "
              f"(seed-searching {n_games} games from {base_seed})...")
        status, detail = verify_card(deck, prof, match["name"], exp, base_seed, n_games, max_turns)
        print(f"  {status}: {detail}")
        return 1 if status == "HARD_MISS" else 0
    if not no_sweep and not os.path.exists(BIN):
        print(f"ERROR: {BIN} not found -- build Release first "
              f"(cmake --build build --config Release), or pass --no-sweep for static-only.")
        return 2

    cards, names = load_deck_cards(deck)
    # Per-card expectations + self-guard.
    expected_types = set()
    per_card = {}
    guard_fail = {}
    for c in cards:
        exp, unmapped = expected_for_card(c)
        exp.discard("main_phase")   # always present; not an interesting expectation
        if exp:
            per_card[c["name"]] = exp
            expected_types |= exp
        if unmapped:
            guard_fail[c["name"]] = unmapped

    print(f"Deck: {deck}  ({len(cards)} card defs matched)")
    print(f"Expected interactive decision types (from card params): "
          f"{sorted(expected_types) or '(none)'}")

    # ---- oracle-text cross-check (advisory, always run -- it is static & instant) ----
    print_oracle_crosscheck(cards)

    # ---- self-guard: unmapped choice params are a hard fail -----------------
    if guard_fail:
        params_to_cards = collections.defaultdict(list)
        for nm, keys in guard_fail.items():
            for k in keys:
                params_to_cards[k].append(nm)
        print("\nSELF-GUARD FAILURE -- UNCLASSIFIED cards.json param(s): present on a deck card but "
              "in NEITHER the decision MANIFEST nor INERT_PARAMS. Each MUST be classified before the "
              "viewer gate can pass -- either it creates a player decision (map it to a type in "
              "MANIFEST + wire it per tools/play/DECISIONS.md), or it creates no choice (add it to "
              "INERT_PARAMS with a reason -- which needs the user's OK):")
        for k in sorted(params_to_cards):
            print(f"  {k}: on {sorted(set(params_to_cards[k]))}")
        return 1

    if no_sweep:
        print("\n--no-sweep: static analysis only (param expectations + oracle cross-check). "
              "Run without --no-sweep to verify decisions actually surface.")
        return 0

    if not expected_types:
        print("No param-driven interactive decisions expected for this deck. PASS.")
        return 0
    if prof is None:
        print("ERROR: a profile path is required for the dynamic sweep "
              "(or pass --no-sweep for static-only).")
        return 2

    print(f"Driving {n_games} games from seed {base_seed} to observe surfaced decisions...")
    observed, cast_text, stuck = run_sweep(deck, prof, base_seed, n_games, max_turns)
    obs_types = set(observed) - {"main_phase", "mulligan", "bottom", "?"}
    print(f"Observed decision types: {sorted(obs_types) or '(none)'}")

    # A driver that never reaches a turn (e.g. an infinite mulligan loop) produces an all-
    # UNVERIFIED result that masquerades as benign. Fail loudly instead.
    if stuck:
        print(f"\nDRIVER FAILURE -- {stuck}/{n_games} games hit the step guard without "
              f"finishing (stuck decision loop; the sweep never reached real turns). "
              f"UNVERIFIED results below are meaningless until this is fixed.")
        if stuck >= max(1, n_games // 2):
            return 1

    # ---- diff -------------------------------------------------------------
    hard_miss, unverified = [], []
    joined = " ".join(cast_text)
    for name, exp in per_card.items():
        missing = exp - obs_types
        if not missing:
            continue
        # Did the sweep actually cast this card? (heuristic: card name appears in a summary)
        cast = name.lower() in joined
        for m in missing:
            (hard_miss if cast else unverified).append((name, m, cast))

    if unverified:
        print("\nUNVERIFIED (card never cast in sweep -- run a targeted 5h repro that casts it):")
        for name, m, _ in unverified:
            print(f"  {name}: expected '{m}' (not reached)")

    if hard_miss:
        print("\nHARD MISS -- card WAS cast but its decision never surfaced "
              "(silently heuristic-resolved; go back to Stage 2c-ter and wire it):")
        for name, m, _ in hard_miss:
            print(f"  {name}: expected '{m}' -> NOT surfaced")
        return 1

    if unverified:
        print("\nNo hard misses. Some expectations UNVERIFIED (see above) -- "
              "confirm with targeted repros before calling 5h clean.")
        return 0

    print("\nAll expected viewer decisions surfaced. 5h PASS.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
