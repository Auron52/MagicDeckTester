#!/usr/bin/env python3
"""Control-scoring mode: which cards are CONTROL-BASED, derived from src/cards/data/cards.json (USER Q11,
docs/design/control-scoring-mode.md; stage-2 design docs/design/control-mode-stage2-report.md section 2).

The USER (2026-11-27): "When screening out a card that is control-based we would need the evidence of this mode
and 1v1 most likely." The classification is DERIVED FROM THE CARD DATA -- the implementation's own effect
parameters, template, types and keywords -- never a hand list of card names. Each card maps to the harness effect
kind(s) its cast would inject into a gauntlet game:

    removal  sweeper  counter_hard  counter_to_hand  counter_to_top  discard  land_destruction
    tax  lock_piece  life_gain  fog  blocker                                      (or none)

Each finding carries a ROLE:
  * primary     the card's own spell / ETB / static / activated effect aimed at the opponent. A primary finding
                makes the card CONTROL-BASED: the deck_compare guard refuses a goldfish-only verdict on a swap
                that adds or removes it (stage-2 report section 7).
  * incidental  a rider on a card whose goldfish job is the clock (burn that may also hit a creature, a threat's
                cast / attack / combat-damage trigger, a life-gain rider on a creature, our own enchantments
                becoming creatures). Listed so the USER can promote it; it does not gate.
and a SOURCE:
  * param:<key>        an implemented effect parameter (the normal case);
  * template:<name>    the card's template (e.g. removal, counter_spell);
  * keyword:<name>     e.g. Defender -> blocker;
  * deck:<rule>        a deck-context rule: two cards' parameters together make a different effect (Enchanted
                       Evening makes every permanent an enchantment, so an enchantment-destroy is land
                       destruction; Opalescence plus Parallax Wave is the exile-for-good loop);
  * param:control_kind a USER correction written into the card's parameters (overrides the rest);
  * oracle(inert)      ONLY for a card the engine does not cast in goldfish (goldfish_inert) and so has no effect
                       parameter: its printed oracle text (the part before the engine's [bracket note]) is matched
                       against a few fixed clause patterns. Flagged so the USER can check it.
A printed control clause the implementation does not model on a card that IS cast (Reality Spasm's tap mode, Force
of Vigor's pitch) is listed under "unmodelled clauses": information, never a classification.

Harness kinds: "proto:" = the prototype grammar of src/core/ControlInject.h (land / creature / bigcrt / nonland /
sweep / discard / bigdisc / counter / skip); "ext:" = an effect the stage-1 extension must add
(docs/design/control-inject-harness.md, being written by cm-engine).

    python3 scripts/control_card_class.py                       # the five stage-2 decks, text report
    python3 scripts/control_card_class.py decks/Hinata ... [--markdown] [--json] [--cards-json PATH]
"""
import json
import os
import re
import sys
from pathlib import Path

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CARDS_JSON = os.path.join(ROOT, "src", "cards", "data", "cards.json")
sys.path.insert(0, os.path.join(ROOT, "scripts"))

DEFAULT_DECKS = ["decks/Enchanted Evening", "decks/Hinata", "decks/Unpredictable Cyclone",
                 "decks/NivMizzetCombo", "decks/Worldfire"]

KINDS = ("removal", "sweeper", "counter_hard", "counter_to_hand", "counter_to_top", "discard",
         "land_destruction", "tax", "lock_piece", "life_gain", "fog", "blocker")

# The harness effect each kind injects (the prototype's grammar where one exists).
HARNESS = {
    "removal": "proto:bigcrt:T / proto:nonland:T:i",
    "removal/exile_until": "ext:exile_until:T (returns when the source leaves)",
    "removal/bounce": "ext:bounce:T (to hand; recast later)",
    "removal/art_ench": "ext:nonland:T:i with an artifact/enchantment filter",
    "sweeper": "proto:sweep:T",
    "sweeper/enchantments": "ext:wipe_type:T:enchantment",
    "sweeper/all_permanents": "ext:wipe:T (every permanent, lands included)",
    "counter_hard": "proto:counter:T:m (ext: trouble-value target, Q8)",
    "counter_to_hand": "ext:counter_to_hand:T",
    "counter_to_top": "ext:counter_to_top:T",
    "discard": "proto:bigdisc:T / proto:discard:T:i",
    "land_destruction": "proto:land:T:i",
    "land_destruction/all": "ext:wipe:T (every land)",
    "tax": "ext:tax:T:n",
    "lock_piece": "ext:lock:T (per lock rule)",
    "lock_piece/wave_opal": "ext:wave_loop:T (exile every creature for good each opponent turn)",
    "life_gain": "ext:lifegain:T:L",
    "fog": "ext:fog:T",
    "fog/tapdown": "ext:tap:T:n (creatures tapped through the opponent's next combat)",
    "blocker": "ext:blocker:T:P/T (needs the declare-blockers step, design 5.6)",
}

# ---------------------------------------------------------------------------------------------------------
# Parameter rules. Each: (regex on the parameter KEY, predicate on the value or None, kind, variant, role,
# detail). Order matters only for readability; every matching rule contributes a finding. role "trigger" means
# "primary unless the card is a creature, then incidental" (a threat's rider).
# ---------------------------------------------------------------------------------------------------------
TRUTHY = lambda v: bool(v)

PARAM_RULES = [
    # removal: single target, our spell / ETB / activation
    (r"^destroy_target_creature(_max_mv)?$", TRUTHY, "removal", "", "primary", "destroy target creature"),
    (r"^revolt_destroy_max_mv$", TRUTHY, "removal", "", "primary", "destroy (revolt widens the MV cap)"),
    (r"^etb_destroy_opp_creature$", TRUTHY, "removal", "", "primary", "ETB destroy an opposing creature"),
    (r"^etb_destroy_nonartifact_nonblack$", TRUTHY, "removal", "", "primary", "ETB destroy a creature"),
    (r"^endstep_(morbid_)?destroy_(target_creature|excluded_subtype)$", TRUTHY, "removal", "", "trigger",
     "end-step destroy target creature"),
    (r"^etb_each_player_sacrifice_creature$", TRUTHY, "removal", "", "primary", "edict (each player sacrifices)"),
    (r"^etb_damage_divided_creatures$", TRUTHY, "removal", "", "primary", "ETB damage divided among creatures"),
    (r"^etb_modal_fight_opp_creature$|^tap_fight_cost$", TRUTHY, "removal", "", "primary", "fight"),
    (r"^etb_opp_creatures_debuff$", TRUTHY, "sweeper", "", "primary", "opposing creatures get -N/-N"),
    (r"^fade_exile_target_creature$", TRUTHY, "removal", "exile_until", "primary",
     "fade counter: exile target creature until the Wave leaves"),
    (r"^etb_exile_nonland_until_leaves$|^etb_or_transform_exile_opp_creature_until_leaves$"
     r"|^etb_exile_opp_nonland_nontoken_max_mv$", TRUTHY, "removal", "exile_until", "primary",
     "exile a nonland permanent until this leaves"),
    (r"^destroy_up_to_artifacts_enchantments$|^modal_exile_target_artifact_enchantment$"
     r"|^etb_destroy_artifact_enchantment(_optional|_land)?$|^remove_counter_destroy_art_ench_cost$"
     r"|^sac_outlet_destroy_artifact_enchantment_optional$", TRUTHY, "removal", "art_ench", "primary",
     "destroy artifact / enchantment"),
    (r"^bounce_opponent_nonland_permanent$|^etb_bounce_nonland$|^cast_trigger_bounce_nonland$",
     TRUTHY, "removal", "bounce", "trigger", "return a nonland permanent to hand"),
    (r"^bounce_all_creatures_except_subtypes$|^saga_ch1_bounce_creatures_except_subtypes$", None,
     "sweeper", "", "primary", "return all (other) creatures to hand"),
    (r"^cast_trigger_(exile|destroy)_target_permanents$", TRUTHY, "removal", "", "trigger",
     "cast trigger: remove target permanent(s)"),
    (r"^saga_ch3_gain_control_opp_permanent$|^steal_tap_count$", TRUTHY, "removal", "", "primary",
     "gain control of an opposing permanent"),
    (r"^annihilator$", TRUTHY, "land_destruction", "", "trigger", "annihilator: the defender sacrifices permanents"),
    # sweepers
    (r"^destroy_all_enchantments$", TRUTHY, "sweeper", "enchantments", "primary", "destroy all enchantments"),
    (r"^destroy_opp_enchantments_then_own_if_untapped_land$", TRUTHY, "sweeper", "enchantments", "primary",
     "destroy all enchantments the opponent controls"),
    (r"^damage_all_creatures(_count_creatures|_x_per_subtype)?$|^x_damage_each_creature_and_player$"
     r"|^damage_opp_creatures_mv_cast$", TRUTHY, "sweeper", "", "primary", "damage to every (opposing) creature"),
    # counters
    (r"^counters_target_spell_to_graveyard$", TRUTHY, "counter_hard", "", "primary", "counter target spell"),
    (r"^counters_target_spell_to_hand$|^bounce_target_spell$|^cast_trigger_bounce_spell_opponent$", TRUTHY,
     "counter_to_hand", "", "trigger", "counter / return target spell to its owner's hand (tempo)"),
    (r"^modal_counter_or_destroy_by_controller_graveyard$", TRUTHY, "removal", "", "primary",
     "modal: destroy target creature (the counter mode is the counters_target_spell param)"),
    # discard
    (r"^endstep_each_player_discards_hand$", TRUTHY, "discard", "", "primary", "each player discards their hand"),
    (r"^combat_damage_each_discards$", TRUTHY, "discard", "", "trigger", "combat damage: each opponent discards"),
    # land destruction
    (r"^sac_outlet_destroy_nonbasic_land$|^etb_destroy_nonbasic_land_optional$", TRUTHY, "land_destruction", "",
     "primary", "destroy a nonbasic land"),
    # tax / lock pieces
    (r"^noncreature_spell_tax$", TRUTHY, "tax", "", "primary", "opponents' noncreature spells cost more"),
    (r"^hinata_cost_reducer$", TRUTHY, "tax", "", "incidental",
     "opponents' spells cost {1} more per target (the cost-reducer half is our engine)"),
    (r"^lands_lose_nonmana_abilities$", TRUTHY, "lock_piece", "", "incidental",
     "lands lose non-mana abilities (fetchlands, utility lands)"),
    (r"^static_players_cant_gain_life$|^players_cant_gain_life_this_turn$|^reflect_player_cant_gain_life$",
     TRUTHY, "lock_piece", "", "incidental", "stops life gain"),
    (r"^ice_counters_dont_untap$", TRUTHY, "fog", "tapdown", "trigger", "tap a creature; it does not untap"),
    (r"^etb_tap_opp_creature$|^saga_ch2_tap_opp_nonland$", TRUTHY, "fog", "tapdown", "trigger",
     "tap opposing permanent(s)"),
    (r"^etb_tap_target_permanent$", TRUTHY, "fog", "tapdown", "incidental", "ETB tap target permanent (a rider)"),
    (r"^static_animate_other_nonaura_enchantments_mv$", TRUTHY, "blocker", "", "incidental",
     "our non-Aura enchantments become creatures (blockers once design 5.6 is built)"),
    (r"^grants_other_enchantments_shroud$", TRUTHY, "lock_piece", "", "incidental",
     "shrouds our enchantments (protects a lock; the gauntlet never targets us, design 6.2)"),
    # fog / prevention
    (r"^sac_outlet_prevent_combat_damage$", TRUTHY, "fog", "", "primary", "prevent combat damage"),
    (r"^prevent_noncombat_to_self_gain$", TRUTHY, "fog", "", "incidental", "prevent noncombat damage to us"),
    (r"^etb_life_floor$", TRUTHY, "life_gain", "", "incidental", "life total floor"),
]

# Burn and pings: removal-capable, incidental (their goldfish job is face damage).
DAMAGE_TARGETING_CREATURE = {"any", "creature", "multi"}
PING_RULES = [r"^tap_ping_damage$", r"^etb_damage_any$", r"^draw_trigger_damage$", r"^dies_trigger_damage$",
              r"^death_trigger_damage$", r"^landfall_damage$", r"^attack_trigger_damage_any$",
              r"^ltb_damage_any$", r"^on_cast_trigger_damage$", r"^remove_counter_ping_damage$"]

# Life gain: our own life. Keys that are NOT our gain (they give the opponent life, or forbid gain) are excluded.
LIFEGAIN_KEY = re.compile(r"lifegain|gain_life|life_gain")
LIFEGAIN_NOT_OURS = re.compile(r"opponent_lifegain|opp_lifegain|lifegain_to_loss|cant_gain|alt_lifegain_cost"
                               r"|alt_lifegain_each_player|controller_lifegain_equals_power|_per_life_gained"
                               r"|mana_per_life_gained|lifegain_counters|lifegain_each_own|lifegain_self_counters"
                               r"|lifegain_target_own_counter|lifegain_each_opp_loses|lifegain_multiplier"
                               r"|lifegain_plus|lifegain_target_opp_loses|lifelink_grant|endstep_lifegain_threshold"
                               r"|life_gated|life_threshold|life_above_start|cast_lifegain_target_player")
LIFELINK_KEY = re.compile(r"grants_lifelink")

# Oracle clause patterns: used for goldfish_inert cards (no effect params) and for the unmodelled-clause listing.
ORACLE_RULES = [
    (re.compile(r"counter target spell.*on top of its owner's library", re.I | re.S), "counter_to_top", ""),
    (re.compile(r"counter target spell.*into its owner's hand", re.I | re.S), "counter_to_hand", ""),
    (re.compile(r"counter target (spell|creature spell|noncreature spell)", re.I), "counter_hard", ""),
    (re.compile(r"(all|each) creatures? get -", re.I), "sweeper", ""),
    (re.compile(r"(destroy|exile) all (creatures|nonland permanents|permanents)", re.I), "sweeper", ""),
    (re.compile(r"destroy all lands|destroy target (nonbasic )?land", re.I), "land_destruction", ""),
    (re.compile(r"return (x |up to \w+ |another )?target (nonland )?(permanent|creature)s?[^.]*to (its|their) "
                r"owners?'? hands?", re.I), "removal", "bounce"),
    (re.compile(r"(destroy|exile) (up to \w+ )?target (creature|nonland permanent|artifact|enchantment|permanent)",
                re.I), "removal", ""),
    (re.compile(r"\btap (x |up to \w+ |two |all )?(target )?(creatures|nonland permanents|permanents)\b", re.I),
     "fog", "tapdown"),
    (re.compile(r"\btap (x |up to \w+ |two )target (creature|permanent)s?\b", re.I), "fog", "tapdown"),
    (re.compile(r"target (player|opponent) discards|each opponent discards", re.I), "discard", ""),
    (re.compile(r"prevent all combat damage", re.I), "fog", ""),
    (re.compile(r"\byou gain \d+ life\b", re.I), "life_gain", ""),
]


def printed_oracle(card):
    """The printed rules text: the engine's notes start at the first '['."""
    return (card.get("oracle_text") or "").split("[", 1)[0]


def _finding(kind, variant, role, source, detail):
    key = kind + ("/" + variant if variant else "")
    return {"kind": kind, "variant": variant, "role": role, "source": source, "detail": detail,
            "harness": HARNESS.get(key) or HARNESS.get(kind)}


def classify_card(card):
    """-> {"name", "findings": [...], "unmodelled": [...], "inert": bool}. Deck context is applied separately."""
    p = card.get("parameters") or {}
    types = set(card.get("types") or [])
    creature = "Creature" in types
    if "control_kind" in p:
        # THE USER-CORRECTION CHANNEL: an explicit list of kinds (["counter_to_top"], or [] for none) in the card's
        # own parameters replaces every derived finding, so a correction lives in the card data, not a name list.
        # Deck-context rules still apply on top. No card sets it today.
        ks = p["control_kind"] if isinstance(p["control_kind"], list) else [p["control_kind"]]
        found = []
        for k in ks:
            kind, _, var = str(k).partition("/")
            if kind not in KINDS:
                raise ValueError("%s: control_kind %r is not one of %s" % (card["name"], k, ", ".join(KINDS)))
            found.append(_finding(kind, var, "primary", "param:control_kind", "set by hand in cards.json"))
        return {"name": card["name"], "findings": found, "unmodelled": [], "inert": bool(p.get("goldfish_inert"))}
    found = []
    for key, val in p.items():
        for rx, pred, kind, var, role, detail in PARAM_RULES:
            if re.search(rx, key) and (pred is None or pred(val)):
                r = role if role != "trigger" else ("incidental" if creature else "primary")
                found.append(_finding(kind, var, r, "param:" + key, detail))
        if any(re.search(rx, key) for rx in PING_RULES) and val:
            found.append(_finding("removal", "", "incidental", "param:" + key, "damage that may target a creature"))
        if LIFEGAIN_KEY.search(key) and not LIFEGAIN_NOT_OURS.search(key) and val:
            # primary only on a pure life-gain SPELL; a land, creature, equipment or burn spell (Lightning Helix)
            # gains life as a rider on its goldfish job (design 5.6: life gain is the effect most likely overrated)
            pure_spell = bool(types & {"Instant", "Sorcery"}) and card.get("template") != "direct_damage"
            found.append(_finding("life_gain", "", "primary" if pure_spell else "incidental", "param:" + key,
                                  "we gain life"))
        if LIFELINK_KEY.search(key) and val:
            found.append(_finding("life_gain", "", "incidental", "param:" + key, "grants lifelink"))
    tmpl = card.get("template")
    if tmpl == "direct_damage" and p.get("targeting") in DAMAGE_TARGETING_CREATURE:
        role = "primary" if p.get("targeting") == "creature" else "incidental"
        found.append(_finding("removal", "", role, "template:direct_damage",
                              "damage, targeting %s" % p.get("targeting")))
    if tmpl == "removal" and not any(f["kind"] == "removal" for f in found):
        var = "bounce" if p.get("bounce_opponent_nonland_permanent") else ""
        found.append(_finding("removal", var, "primary", "template:removal",
                              "removal, targeting %s" % p.get("targeting")))
    if tmpl == "counter_spell" and not any(f["kind"].startswith("counter") for f in found):
        found.append(_finding("counter_hard", "", "primary", "template:counter_spell", "counter target spell"))
    if "Defender" in (card.get("keywords") or []):
        found.append(_finding("blocker", "", "primary", "keyword:Defender", "a creature that only blocks"))
    if "exile_all_permanents_hands_graveyards" in p:
        # GOLDFISH-CREDITED: it leaves the opponent no lands, no hand and no creature, which the free-turn rule
        # (src/core/FreeTurns.h) credits from the opponent's REAL state. The USER kept Worldfire in goldfish
        # ("close enough"), so the deck_compare guard does not demand control evidence for it.
        for kind, var, detail in (("sweeper", "all_permanents", "exile every permanent, hand and graveyard"),
                                  ("land_destruction", "all", "every land exiled"),
                                  ("discard", "", "every hand exiled")):
            f = _finding(kind, var, "primary", "param:exile_all_permanents_hands_graveyards", detail)
            f["goldfish_credited"] = True
            found.append(f)
    inert = bool(p.get("goldfish_inert"))
    unmodelled = []
    text = printed_oracle(card)
    fam = lambda k: "counter" if k.startswith("counter") else k     # the three counter kinds are one family
    for rx, kind, var in ORACLE_RULES:
        if rx.search(text):
            if any(fam(f["kind"]) == fam(kind) for f in found):
                continue
            if inert:
                found.append(_finding(kind, var, "primary", "oracle(inert)", "printed clause; goldfish never "
                                      "casts it, so it has no effect parameter"))
            elif not any(fam(u["kind"]) == fam(kind) for u in unmodelled):
                unmodelled.append({"kind": kind, "variant": var, "clause": rx.search(text).group(0)})
    return {"name": card["name"], "findings": _dedupe(found), "unmodelled": unmodelled, "inert": inert}


def _dedupe(found):
    out, seen = [], set()
    for f in found:
        k = (f["kind"], f["variant"], f["role"], f["source"])
        if k not in seen:
            seen.add(k)
            out.append(f)
    return out


def apply_deck_context(classes, cards):
    """Two cards' parameters together: rules keyed on parameters present ANYWHERE in the deck's main board."""
    present = {}
    for n in classes:
        for k, v in (cards[n].get("parameters") or {}).items():
            if v:
                present.setdefault(k, []).append(n)
    ee = "static_all_permanents_enchantments" in present
    opal = "static_animate_other_nonaura_enchantments_mv" in present
    wave = "fade_exile_target_creature" in present
    ench_destroy = ("destroy_up_to_artifacts_enchantments", "destroy_opp_enchantments_then_own_if_untapped_land",
                    "destroy_all_enchantments", "modal_exile_target_artifact_enchantment",
                    "etb_destroy_artifact_enchantment", "etb_destroy_artifact_enchantment_optional")
    for n, c in classes.items():
        p = cards[n].get("parameters") or {}
        add = []
        if ee and any(p.get(k) for k in ench_destroy):
            mass = p.get("destroy_opp_enchantments_then_own_if_untapped_land") or p.get("destroy_all_enchantments")
            src = "deck:with static_all_permanents_enchantments (%s)" % ", ".join(present[
                "static_all_permanents_enchantments"])
            if mass:
                add.append(_finding("sweeper", "all_permanents", "primary", src,
                                    "every opposing permanent is an enchantment: a wipe, lands included"))
                add.append(_finding("land_destruction", "all", "primary", src, "every opposing land destroyed"))
            else:
                add.append(_finding("land_destruction", "", "primary", src,
                                    "lands are enchantments: may destroy up to the target count of lands"))
        if ee and opal and (p.get("static_all_permanents_enchantments")
                            or p.get("static_animate_other_nonaura_enchantments_mv")):
            add.append(_finding("land_destruction", "all", "primary",
                                "deck:static_all_permanents_enchantments + static_animate_other_nonaura_enchantments_mv",
                                "every land on BOTH sides is a 0/0 creature and dies (ours too)"))
            add.append(_finding("lock_piece", "", "primary",
                                "deck:static_all_permanents_enchantments + static_animate_other_nonaura_enchantments_mv",
                                "every later land and MV-0 token dies as it enters"))
        if "reflect_dealt_damage" in present and any(p.get(k) for k in (
                "damage_all_creatures", "damage_all_creatures_count_creatures", "x_damage_each_creature_and_player")):
            # damage to every creature in a deck whose creatures REFLECT damage (Stuffy Doll) is that deck's clock
            src = "deck:with reflect_dealt_damage (%s)" % ", ".join(present["reflect_dealt_damage"])
            for f in c["findings"]:
                if f["kind"] == "sweeper" and f["role"] == "primary":
                    f["role"] = "incidental"
                    f["detail"] += "; demoted: the reflectors make it this deck's damage engine"
                    f["source"] += " + " + src
        if opal and wave and (p.get("static_animate_other_nonaura_enchantments_mv")
                              or p.get("fade_exile_target_creature")):
            add.append(_finding("lock_piece", "wave_opal", "primary",
                                "deck:fade_exile_target_creature + static_animate_other_nonaura_enchantments_mv",
                                "the Wave self-exile loop exiles opposing creatures for good (design 4.3a)"))
        c["findings"] = _dedupe(c["findings"] + add)
    return classes


def is_control_based(cls):
    """True when a primary finding exists that goldfish does NOT already credit (the deck_compare guard's test)."""
    return any(f["role"] == "primary" and not f.get("goldfish_credited") for f in cls["findings"])


def is_goldfish_credited(cls):
    return any(f.get("goldfish_credited") for f in cls["findings"])


def load_cards(path=CARDS_JSON):
    with open(path) as f:
        return {c["name"]: c for c in json.load(f)["cards"]}


def deck_file(path):
    """A deck folder or decklist -> (decklist path, deck name)."""
    p = Path(path if os.path.isabs(path) else os.path.join(ROOT, path))
    if p.is_file():
        return p, p.parent.name
    for ext in (".cod", ".txt"):
        f = p / (p.name + ext)
        if f.exists():
            return f, p.name
    raise SystemExit("no decklist in %s" % p)


def classify_deck(path, cards):
    from analyze_deck import LoadDeckCounts
    f, name = deck_file(path)
    counts = LoadDeckCounts(f, board="main")
    classes, missing = {}, []
    for n in counts:
        if n not in cards:
            missing.append(n)
            continue
        classes[n] = classify_card(cards[n])
    apply_deck_context(classes, cards)
    rows = []
    for n, cnt in counts.items():
        if n in classes:
            c = classes[n]
            rows.append({"card": n, "count": cnt, "control_based": is_control_based(c),
                         "goldfish_credited": is_goldfish_credited(c), **c})
    return {"deck": name, "file": str(f), "rows": rows, "missing": missing}


def summary(rep):
    ctrl = [r for r in rep["rows"] if r["control_based"]]
    inc = [r for r in rep["rows"] if not r["control_based"] and r["findings"] and not r["goldfish_credited"]]
    gfc = [r for r in rep["rows"] if not r["control_based"] and r["goldfish_credited"]]
    by_kind = {}
    for r in ctrl:
        for k in sorted({f["kind"] for f in r["findings"] if f["role"] == "primary"}):
            by_kind[k] = by_kind.get(k, 0) + r["count"]
    return {"control_cards": len(ctrl), "control_copies": sum(r["count"] for r in ctrl),
            "incidental_cards": len(inc), "incidental_copies": sum(r["count"] for r in inc),
            "main_cards": len(rep["rows"]), "main_copies": sum(r["count"] for r in rep["rows"]),
            "goldfish_credited_cards": len(gfc), "goldfish_credited_copies": sum(r["count"] for r in gfc),
            "by_kind_copies": by_kind,
            "unmodelled": sum(1 for r in rep["rows"] if r["unmodelled"])}


def _kinds(r, role):
    ks = []
    for f in r["findings"]:
        if f["role"] == role:
            k = f["kind"] + ("/" + f["variant"] if f["variant"] else "")
            if k not in ks:
                ks.append(k)
    return ks


def _sources(r):
    out = []
    for f in r["findings"]:
        if f["source"] not in out:
            out.append(f["source"])
    return out


def format_text(rep):
    s = summary(rep)
    lines = ["== %s  (%s)" % (rep["deck"], os.path.relpath(rep["file"], ROOT)),
             "   control-based: %d cards / %d copies;  incidental only: %d cards / %d copies;  of %d cards / %d copies"
             % (s["control_cards"], s["control_copies"], s["incidental_cards"], s["incidental_copies"],
                s["main_cards"], s["main_copies"])]
    for r in rep["rows"]:
        if not r["findings"] and not r["unmodelled"]:
            continue
        tag = "CONTROL" if r["control_based"] else ("gf-cred" if r["goldfish_credited"]
                                                    else ("incid. " if r["findings"] else "none   "))
        lines.append("   %s %dx %-26s primary=%s incidental=%s  [%s]"
                     % (tag, r["count"], r["card"], ",".join(_kinds(r, "primary")) or "-",
                        ",".join(_kinds(r, "incidental")) or "-", "; ".join(_sources(r))))
        for u in r["unmodelled"]:
            lines.append("            unmodelled clause (%s): \"%s\"" % (u["kind"], u["clause"]))
    nonec = [r["card"] for r in rep["rows"] if not r["findings"] and not r["unmodelled"]]
    lines.append("   none: " + ", ".join(nonec))
    if rep["missing"]:
        lines.append("   NOT IN cards.json: " + ", ".join(rep["missing"]))
    return "\n".join(lines)


def format_markdown(rep):
    s = summary(rep)
    lines = ["**%s** (`%s`): %d control-based cards / %d copies; %d incidental-only cards / %d copies; %d of %d "
             "main-board cards have no finding." % (rep["deck"], os.path.relpath(rep["file"], ROOT),
                                                    s["control_cards"], s["control_copies"], s["incidental_cards"],
                                                    s["incidental_copies"],
                                                    sum(1 for r in rep["rows"] if not r["findings"]), s["main_cards"]),
             "",
             "| card | n | control-based | primary kinds | incidental kinds | source | harness |",
             "|---|---|---|---|---|---|---|"]
    for r in rep["rows"]:
        if not r["findings"] and not r["unmodelled"]:
            continue
        harn = []
        for f in r["findings"]:
            if f["role"] == "primary" and f["harness"] and f["harness"] not in harn:
                harn.append(f["harness"])
        lines.append("| %s | %d | %s | %s | %s | %s | %s |"
                     % (r["card"], r["count"], "**yes**" if r["control_based"] else "no",
                        ", ".join(_kinds(r, "primary")) or "-", ", ".join(_kinds(r, "incidental")) or "-",
                        "; ".join("`%s`" % x for x in _sources(r)) or "-", "; ".join(harn) or "-"))
        if r["goldfish_credited"]:
            lines[-1] = lines[-1].replace("| no |", "| no (goldfish-credited) |", 1)
        for u in r["unmodelled"]:
            lines.append("| ↳ unmodelled clause | | | | %s | printed: \"%s\" | |" % (u["kind"], u["clause"]))
    nonec = [r["card"] for r in rep["rows"] if not r["findings"] and not r["unmodelled"]]
    lines += ["", "No finding: " + ", ".join(nonec) + "."]
    if rep["missing"]:
        lines.append("Not in cards.json (skipped): " + ", ".join(rep["missing"]) + ".")
    return "\n".join(lines)


def main(argv):
    md = "--markdown" in argv
    js = "--json" in argv
    cj = argv[argv.index("--cards-json") + 1] if "--cards-json" in argv else CARDS_JSON
    skip = {"--markdown", "--json", "--cards-json", cj}
    decks = [a for a in argv if a not in skip] or DEFAULT_DECKS
    cards = load_cards(cj)
    reps = [classify_deck(d, cards) for d in decks]
    if js:
        print(json.dumps([{**r, "summary": summary(r)} for r in reps], indent=1))
        return 0
    for r in reps:
        print(format_markdown(r) if md else format_text(r))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
