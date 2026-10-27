#!/usr/bin/env python3
"""PROMOTE A SEARCH LINE TO A REFERENCE -- re-express the search's game as a play-viewer recording.

The review viewer (tools/replay/review.html) shows a reference game beside the shipped search's game.
When the user judges the search's line the better one, "Promote" makes it THE reference for that game:
the file the bench, the protocol sweep and the viewer's maturity badges all read.

A reference is a play-viewer DECISION STREAM (the plan index picked at each frame), while the search's
game is an autonomous action log, so promotion is a translation, done by playing the game again in
claude-play mode and choosing, at each main-phase frame, the enumerated plan that does what the search
did in that phase:

  * the human's recorded picks are replayed first when the source is a hand-off (`handoff_T<k>.json`:
    your turns 1..k-1, then the search), so the promoted game keeps your opening turns;
  * at each main-phase frame, among the plans that use only cards the search used in that phase, the
    one whose PROBED result (played on to the next main-phase frame) comes closest to the search's
    end-of-phase board (battlefield, hand, opponent life) is committed; pass when nothing gets closer.
    A plan that reaches the search's own win is taken at once (see walk() for why names alone fail).
    A cast at an X the search did not use is not the search's action: a plan's X is fixed from the mana
    in hand when it is enumerated, so a bigger X can need an earlier segment (lifegain for Accomplished
    Alchemist, SelesnyaLifegain s15_gi14, 2026-10-25);
  * every other frame (a target, a tutor pick, a scry, a put-into-play) is answered the same way: each
    single-pick answer is probed and the one whose board at the next main-phase frame is closest to
    the search's is taken (multi-pick frames take the engine's default). Answers that tie there are
    probed on to the next turn's first main phase: a card tutored to the top of the library is hidden
    until it is drawn (Worldly Tutor, StompySurprise s1_gi0, 2026-10-25);
  * the first point where the replay's board drifts from the search's is noted, and named in the
    refusal if the line then misses the search's win.

A promotion is written only if the result is a FAITHFUL, LITERAL reference: the engine-written trace
(the same writer the viewer's save uses) wins on exactly the search's win turn and round-trips through
test/viewer_protocol_check.check_reference as plain `ok`. Anything else is refused with the reason --
"replay it by hand in the play viewer" is then the route.

    python3 scripts/ref_promote.py treasure_hunt/claude_s5_gi4.json                 # dry run, search from T1
    python3 scripts/ref_promote.py treasure_hunt/claude_s5_gi4.json --source handoff_T4.json
    python3 scripts/ref_promote.py <key> --source <file> --apply --json             # what review_server runs
    python3 scripts/ref_promote.py --check-all                                      # dry-run every review game

--apply writes references/<deck>/claude_s<S>_gi<G>.json. The human's own line is NEVER lost: before it
is replaced, its exact bytes are copied to references/review/replaced/<deck>/human_s<S>_gi<G>.json, and
both files are listed in the JSON output for the caller to back up + commit + push (review_server.js does, through tools/play/refsafe.js), as is the
`promoted` verdict this records in references/review/verdicts.json.

WHY THAT PATH, and not references/suboptimal/: suboptimal/ is the user's note that the VIEWER would not
let them reach the optimal turn (a target the search must beat), not an archive of beaten lines. Under
references/ the copy is still covered by scripts/check_references_safe.py; the `human_` prefix keeps it
out of every `claude_s*_gi*.json` glob, so neither the bench nor the protocol sweep reads it as a live
reference.
"""
import argparse
import collections
import concurrent.futures
import datetime
import itertools
import json
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
sys.path.insert(0, os.path.join(ROOT, "test"))
import ref_review as rr                      # noqa: E402
import viewer_protocol_check as vpc          # noqa: E402
import combo_off_replay_hunt as hunt         # noqa: E402

SCRATCH = os.path.join("build", "scratch", "ref_promote")
REPLACED = os.path.join("references", "review", "replaced")   # the human lines a promotion replaced
VERBOSE = False       # --verbose: every main-phase frame's probes and choice, to stderr


def say(*a):
    if VERBOSE:
        print(*a, file=sys.stderr, flush=True)
PHASE_OF = {"MAIN_1": "pre_main", "COMBAT": "pre_main", "MAIN_2": "post_main", "CLEANUP": "post_main"}
LEGACY_PREFIX = "--legacy-"
MAX_PROBES = 32       # candidate plans probed per frame (each probe is one game replay)


class Refused(Exception):
    pass


# ---- the search log ---------------------------------------------------------------------------

# Action kinds that are not something a plan does (draws, reveals, damage lines, discards forced by a
# resolving effect): every other named action is a card the search used that phase.
NOT_PLAYED = {"DRAW", "REVEAL", "ATTACK", "DISCARD"}


def phase_names(log, from_turn):
    """{(turn, phase): {'lands': set of land-drop names, 'cards': set of other card names,
    'count': Counter of 'land:<name>' / '<name>' per named action, 'xs': {name: set of X the search cast
    it with}} -- the cards the search used in each main phase. Combat-step casts (tricks) belong to the
    pre-combat plan, as the viewer's plans hold them."""
    out = collections.defaultdict(lambda: {"lands": set(), "cards": set(), "count": collections.Counter(),
                                           "xs": collections.defaultdict(set), "found": [], "seq": [],
                                           "oseq": []})
    names_of = card_names(log)
    LOG_LANDS.clear()
    prev_hand, prev_bf, cur_bf = None, None, None
    for s in log.get("turns") or []:
        ph = PHASE_OF.get(s.get("phase"))
        hand = (s.get("boardAfter") or {}).get("hand")
        was, prev_hand = prev_hand, hand
        prev_bf, cur_bf = cur_bf, (s.get("boardAfter") or {}).get("battlefield")
        if ph is None or s.get("turn", 0) < from_turn:
            continue
        # A card that left the hand with no action naming it (a SUSPENDED Search for Tomorrow: the
        # log records the cast only when it comes off suspend, Landfall s2_gi1 T4) is a card the
        # search used: a plan may play it (permission only -- it is not counted as progress).
        if was is not None and hand is not None and s.get("phase") in ("MAIN_1", "MAIN_2"):
            named = {a.get("card") for a in s.get("actions") or []}
            named |= {x.get("card") for a in s.get("actions") or [] for x in a.get("lookedAt") or []}
            for num in (collections.Counter(was) - collections.Counter(hand)).elements():
                if num not in named and names_of.get(num):
                    out[(s["turn"], ph)]["cards"].add(names_of[num])
        # A LAND that went from the battlefield to the graveyard with no action naming it: a land-search
        # crack (Myriad Landscape, Evolving Wilds) the executor ran at its searched PRE-CAST position
        # through TurnSolver::ApplyLandSearchActs, which did not log (LandfallElementals s10_gi9 T4,
        # 2026-10-26). Ahead of the phase's named actions -- where slot 0 runs it. The engine logs
        # these as ABILITY entries since 2026-10-27; this covers logs written before that.
        if prev_bf is not None and s.get("phase") in ("MAIN_1", "MAIN_2"):
            b = s.get("boardAfter") or {}
            now = {x.get("card") for x in b.get("battlefield") or []}
            gy = set(b.get("graveyard") or [])
            named = {a.get("card") for a in s.get("actions") or []}
            for x in prev_bf:
                if (x.get("isLand") and x.get("card") not in now and x.get("card") in gy
                        and x.get("card") not in named and cracks(x.get("cardName"))):
                    o, n = out[(s["turn"], ph)], x.get("cardName")
                    o["cards"].add(n)
                    o["count"][n] += 1
                    o["seq"].insert(0, n)
                    o["oseq"].insert(0, n)
        for a in s.get("actions") or []:
            # What the search's tutors fetched (Living Wish -> Adarkar Wastes), in order: the plan's
            # tutor target is the search's action as much as the cast is.
            if a.get("type") == "REVEAL" and str(a.get("source") or "").endswith("(searched)"):
                kept = set(a.get("kept") or [])
                # (tutor, card): Natural Order's put is not a "(searched)" reveal, so a plan's Natural
                # Order -> Priest of Titania must not claim Worldly Tutor's Priest (StompySurprise
                # s2_gi1 T3, 2026-10-27).
                src = str(a.get("source"))[:-len(" (searched)")]
                out[(s["turn"], ph)]["found"] += [(src, x.get("cardName")) for x in a.get("lookedAt") or []
                                                  if x.get("card") in kept and x.get("cardName")]
            n = a.get("cardName")
            if not n or a.get("type") in NOT_PLAYED:
                continue
            land = a.get("type") == "PLAY_LAND"
            if land:
                # The FACE in play: the log names a modal double-faced card's front even when its land
                # back face was played ("Turntimber Symbiosis" for Turntimber, Serpentine Wood), while
                # plan_land reads the face the plan plays (StompySurprise s2_gi1 T2, 2026-10-27).
                n = next((x.get("cardName") for x in (s.get("boardAfter") or {}).get("battlefield") or []
                          if x.get("card") == a.get("card") and x.get("cardName")), n)
            if land:
                LOG_LANDS.add(n)
            out[(s["turn"], ph)]["lands" if land else "cards"].add(n)
            k = "land:" + n if land else log_key(a)
            out[(s["turn"], ph)]["count"][k] += 1
            out[(s["turn"], ph)]["oseq"].append(k)
            if not land:
                out[(s["turn"], ph)]["seq"].append(k)
            if a.get("type") == "CAST_SPELL" and isinstance(a.get("chosenX"), int):
                out[(s["turn"], ph)]["xs"][n].add(a["chosenX"])
    return out


def log_key(a):
    """The key of a named log action: an equip activation is "equip:<Equipment>", apart from a CAST of it."""
    n = a.get("cardName")
    return "equip:" + n if a.get("type") == "ABILITY" and str(a.get("ability") or "").startswith("equip") else n


def plan_key(a):
    """The same key for a plan action. `equip all free` names its HOST as the card, so it is keyed apart from
    a cast of that creature too ("equipall:")."""
    v = a.get("verb")
    if v == "equip":
        return "equip:" + a.get("card")
    if v == "equipallfree":
        return "equipall:" + a.get("card")
    # Any other activation is logged under the permanent that ACTIVATES it: "put Fury onto the battlefield
    # (Incandescent Soulstoke)" is the log's ABILITY Incandescent Soulstoke, not a Fury (LandfallElementals
    # s3_gi2 handoff_T2 T4, 2026-10-27).
    if a.get("activate") and a.get("activate_source"):
        return a["activate_source"]
    return a.get("card")


_CRACKS = None


def cracks(name):
    """Does this land have a sacrifice-to-search activation (cards.json land_search_activation_sacrifice_self)?
    A land that went to the graveyard any other way went by itself: Remote Farm is sacrificed when its last
    depletion counter goes, and counting that as an unplayed action left it in `rest` for good -- no plan
    plays it, so the walk could never pass and moved Cathar's Shield onto Cid and back until the frame cap
    (KittyEquipment v2 s13_gi12 T3)."""
    global _CRACKS
    if _CRACKS is None:
        data = json.load(open(os.path.join(ROOT, "src", "cards", "data", "cards.json")))
        _CRACKS = {c.get("name") for c in data.get("cards") or []
                   if (c.get("parameters") or {}).get("land_search_activation_sacrifice_self")}
    return name in _CRACKS


def card_names(log):
    """{card number: name} from everything the log names (opening hand, draws, actions, reveals)."""
    out = {}
    for c in log.get("openingHand") or []:
        out[c.get("card")] = c.get("cardName")
    for st in log.get("turns") or []:
        for a in st.get("actions") or []:
            if a.get("card") is not None and a.get("cardName"):
                out.setdefault(a["card"], a["cardName"])
            for x in a.get("lookedAt") or []:
                out.setdefault(x.get("card"), x.get("cardName"))
        for x in (st.get("boardAfter") or {}).get("battlefield") or []:
            out.setdefault(x.get("card"), x.get("cardName"))
    return out


LOG_LANDS = set()   # every land name the log being promoted plays (phase_names fills it)


def plan_land(p):
    """The land the plan really plays: a modal double-faced land's BACK face is named only in the summary
    ("land=Branchloft Pathway -> Boulderloft Pathway (back face)"), and the search log names the face.
    Except when it does not: a spell // land card logs its FRONT ("Turntimber Symbiosis" for Turntimber,
    Serpentine Wood), and when the land leaves play the same phase (Terastodon) no board shows the face.
    So the name the log uses wins (StompySurprise s2_gi1 handoff_T3, 2026-10-27)."""
    if not p.get("land"):
        return None
    m = re.match(r"land=[^;]*? -> (.+?) \(back face\)", p.get("summary") or "")
    if m and m.group(1) not in LOG_LANDS and p["land"] in LOG_LANDS:
        return p["land"]
    return m.group(1) if m else p["land"]


def plays(p):
    """The plan's actions it really performs, in the order it performs them: a cast listed in "drops" is
    one it cannot make (the plan keeps its name for the menu; SelesnyaLifegain s15_gi14 T5 back-face plans
    drop Feed the Clan), and a combat TRICK is cast in combat, after the main-phase casts, wherever the
    plan lists it ("Expedite -> Goblin Instigator #1, Goblin Instigator": Mirrorwing s8_gi7 T3,
    2026-10-27)."""
    drops = collections.Counter(p.get("drops") or [])
    out = []
    for a in p.get("actions") or []:
        if not a.get("activate") and drops[a.get("card")] > 0:
            drops[a.get("card")] -= 1
            continue
        out.append(a)
    return [a for a in out if not a.get("trick")] + [a for a in out if a.get("trick")]


def plan_seq(p):
    """The plan's actions in execution order as tokens ('<card>' / 'land:<name>'), the land drop at its
    searched position (land_after_casts / land_slot k = after the k-th landfall-watcher cast, named in
    the summary / karoo_before r = before the r-th cast)."""
    acts = [plan_key(a) for a in plays(p) if a.get("card")]
    if not p.get("land"):
        return acts
    at = 0
    if p.get("land_after_casts"):
        at = len(acts)
    elif p.get("land_slot"):
        m = re.search(r"\(after ([^)]*)\)", p.get("summary") or "")
        at = len(acts)
        for i, n in enumerate(acts):
            if m and n == m.group(1) and i >= int(p["land_slot"]) - 1:   # the k-th watcher, by name
                at = i + 1
                break
    elif p.get("karoo_before"):
        at = max(0, min(len(acts), int(p["karoo_before"]) - 1))
    return acts[:at] + ["land:" + plan_land(p)] + acts[at:]


def seq_match(p, seq, xs=None):
    """Positions in `seq` of the plan's actions (each matched to its first unused occurrence), in the
    plan's order; actions at a wrong X do not match."""
    pos, taken = collections.defaultdict(list), collections.Counter()
    for i, t in enumerate(seq or []):
        pos[t].append(i)
    ok = content(p, xs)
    out = []
    for t in plan_seq(p):
        if ok[t] > 0 and taken[t] < len(pos[t]):
            out.append(pos[t][taken[t]])
            taken[t] += 1
            ok[t] -= 1
    return out


def order_cost(p, seq, xs=None):
    """Pairs of the plan's actions done in the opposite order from the search's (`seq`: its named
    actions this phase, lands included, in log order), plus the search's actions the plan jumps over.
    Plans that differ only in ORDER can leave the same end-of-phase board but not the same game: a
    land played after Brushfire Elemental pumps it for the attack, played before it does not
    (LandfallElementals s8_gi7 T3, 2026-10-26: 4 damage instead of the search's 6)."""
    idx = seq_match(p, seq, xs)
    inv = sum(1 for i in range(len(idx)) for j in range(i + 1, len(idx)) if idx[i] > idx[j])
    return inv + ((max(idx) + 1 - len(idx)) if idx else 0)


def boards(log):
    """{(turn, phase): (board BEFORE the phase, board AFTER it)} from the log's step boards."""
    out, prev = {}, None
    for s in log.get("turns") or []:
        ph = PHASE_OF.get(s.get("phase"))
        if s.get("phase") in ("MAIN_1", "MAIN_2"):
            out[(s["turn"], ph)] = (prev, s.get("boardAfter"))
        prev = s.get("boardAfter")
    return out


def bf_names(rows, name, host):
    """Battlefield names, an attached Aura / Equipment as "<name>@<host name>#<host card number>": WHICH
    creature carries it is the line (Bruna s14_gi13, 2026-10-26: Mythic Proportions on Avacyn's Pilgrim
    matched the search's board on Linvala at distance 0, and the T4 swing dealt 3 instead of 11), and the
    host's NAME cannot tell two copies apart (KittyEquipment v1 s6_gi5 T4: Lightning Greaves on the Kor
    Duelist already carrying Bonesplitter matched at distance 0 and the swing dealt 7, not 8)."""
    by_num = {r.get("num", r.get("card")): r.get(name) for r in rows}
    return collections.Counter(r.get(name) + ("@%s#%s" % (by_num.get(r[host], "?"), r[host]) if r.get(host) else "")
                               for r in rows)


def log_sig(b):
    """(battlefield names, hand card numbers, opponent life, own life) of a log board."""
    if not b:
        return None
    return (bf_names(b.get("battlefield") or [], "cardName", "attachedTo"),
            collections.Counter(b.get("hand") or []), b.get("opponentLife"), b.get("playerLife"))


def gy_size(me):
    """Graveyard size of a frame's `me` block."""
    if isinstance(me, str):
        me = json.loads(me)
    return len(me.get("graveyard") or [])


def frame_sig(d):
    """The same signature for a claude-play decision frame (or result frame)."""
    me, opp = d.get("me") or {}, d.get("opponent") or {}
    if isinstance(me, str):
        me = json.loads(me)
    if isinstance(opp, str):
        opp = json.loads(opp)
    return (bf_names(me.get("battlefield") or [], "name", "attached_to"),
            # Cards exiled by Light Up the Stage sit in the frame's hand marked is_staged; the log's hand
            # never lists them (burn s8_gi7 / s21_gi20, 2026-10-26).
            collections.Counter(c.get("num") for c in me.get("hand") or []
                                if isinstance(c, dict) and not c.get("is_staged")),
            opp.get("life"), me.get("life"))


def projected(d, left, target):
    """frame_sig of a frame MID-phase, with every card in its hand that the search still has to play
    this phase (`left`, by name) counted as played: out of the hand (unless the search's end-of-phase
    board `target` holds that very card in hand -- a Remanded spell comes back), onto the battlefield
    where `target` has room for it. Compared with the search's END-of-phase board, a card drawn this
    phase and not yet played otherwise reads as a mismatch, so the answer that draws fewer cards looks
    closer (an Expedite on Zada or Mirrorwing Dragon, whose copy draws the land the search then played:
    Mirrorwing s19_gi18, s6_gi5; a Living Wish for a land: AccomplishedAlchemist s4_gi3, 2026-10-26).
    The search's still-unplayed actions NOT in hand are a fifth term, a unit of distance each."""
    bf, hand, life, mine = frame_sig(d)
    if not +left:
        return bf, hand, life, mine, 0
    me = d.get("me") or {}
    if isinstance(me, str):
        me = json.loads(me)
    left = collections.Counter(left)
    for c in me.get("hand") or []:
        if not isinstance(c, dict) or c.get("is_staged"):
            continue
        k = ("land:" if c.get("kind") == "land" else "") + (c.get("name") or "")
        if left[k] > 0 and hand[c.get("num")] > 0:
            left[k] -= 1
            if not target[1][c.get("num")]:
                hand[c.get("num")] -= 1
                if target[0][c.get("name")] > bf[c.get("name")]:
                    bf[c.get("name")] += 1
    return bf, +hand, life, mine, sum((+left).values())


def sig_diff(a, b):
    """-> (distance, text) between two signatures."""
    bf = (a[0] - b[0]) + (b[0] - a[0])
    hd = (a[1] - b[1]) + (b[1] - a[1])
    life = abs((a[2] or 0) - (b[2] or 0)) if a[2] is not None and b[2] is not None else 0
    # Own life: a shock land's pay-2 (land_entry) shows nowhere else until the untapped mana is spent
    # (FiveColour s13_gi12 T1: Stomping Ground untapped for Birds of Paradise in main 2).
    mine = abs(a[3] - b[3]) if a[3] is not None and b[3] is not None else 0
    txt = []
    if a[0] != b[0]:
        txt.append("battlefield: replay has %s, search had %s" % (
            sorted((a[0] - b[0]).elements()) or "-", sorted((b[0] - a[0]).elements()) or "-"))
    if a[1] != b[1]:
        txt.append("hand: replay has #%s, search had #%s" % (
            sorted((a[1] - b[1]).elements()) or "-", sorted((b[1] - a[1]).elements()) or "-"))
    if life:
        txt.append("opponent life: replay %s, search %s" % (a[2], b[2]))
    if mine:
        txt.append("own life: replay %s, search %s" % (a[3], b[3]))
    owed = a[4] if len(a) > 4 else 0                  # projected(): the search's actions not in hand
    if owed:
        txt.append("%d of the search's actions this phase not in hand" % owed)
    return sum(bf.values()) + sum(hd.values()) + life + mine + owed, "; ".join(txt)


OPTION_LISTS = ("options", "candidates", "colors", "pool")
BINARY = {"vial_charge", "echo", "demonstrate", "land_entry"}


def option_picks(d):
    """The single-pick answers a non-main-phase frame offers, engine default first; [] when the frame
    is a multi-pick (a list `chosen`) or has nothing to choose between -- those take the default."""
    t = d.get("type")
    dflt = vpc.engine_default(d)[0]
    if t == "x_value" and isinstance(d.get("min_x"), int) and isinstance(d.get("max_x"), int):
        opts = list(range(d["min_x"], d["max_x"] + 1))
    elif t == "replicate" and isinstance(d.get("max_count"), int):
        opts = list(range(0, d["max_count"] + 1))
    elif t in BINARY:
        opts = [0, 1]
    elif t == "dig" and isinstance(d.get("examined"), list):
        # Which card goes on top (Brainstorm's put-back, then the one beneath it) or which examined card
        # is taken: the put-back ORDER is the next draws (NivMizzetCombo s4_gi3, 2026-10-26).
        opts = [i for i, x in enumerate(d["examined"]) if not isinstance(x, dict) or x.get("legal", True)]
    else:
        lst = next((d[k] for k in OPTION_LISTS if isinstance(d.get(k), list)), None)
        opts = list(range(len(lst))) if lst else []
    if len(opts) < 2:
        return []
    return [dflt] + [o for o in opts if o != dflt]


FLAG_SUBSETS = 5000   # candidate subsets enumerated at a flags frame; beyond it only the AI's set is offered


def is_flags(d):
    """A put-any-subset frame (dragon / sac_tutor / revive / rummage): the reply is ONE 0/1 flag per
    candidate, read together; interactive mode re-emits the frame for each flag until it has them all."""
    return (isinstance(d.get("max_puts"), int) and isinstance(d.get("candidates"), list)
            and isinstance(d.get("ai_set"), list))


def default_answer(d):
    """The engine's own answer to a non-main frame, as the list of ints it is sent as: a flags frame's
    whole vector (vpc.engine_flags: the AI's set, Defense of the Heart / Soulfire), else one pick."""
    fl = vpc.engine_flags(d)
    return fl if fl is not None else [vpc.engine_default(d)[0]]


def answers(d):
    """The answers a non-main frame offers, each as the list of ints it is sent as, engine default
    first; [] when there is nothing to choose between (take the default). A flags frame offers every
    distinct SEQUENCE of candidate names up to max_puts: copies of one card are interchangeable, but the
    picks enter in ascending candidate order and the order is the line (Hunted Phantasm before Massacre
    Wurm kills the Phantasm's Faeries; Creature_Giving s2_gi1)."""
    if not is_flags(d):
        return [[o] for o in option_picks(d)]
    names = [x.get("name") for x in d["candidates"]]
    k = min(d["max_puts"], len(names))
    if sum(math.comb(len(names), r) for r in range(k + 1)) > FLAG_SUBSETS:
        return []
    out, seen = [default_answer(d)], set()
    seen.add(tuple(n for n, f in zip(names, out[0]) if f))
    for r in range(k, -1, -1):
        for combo in itertools.combinations(range(len(names)), r):
            key = tuple(names[i] for i in combo)
            if key not in seen:
                seen.add(key)
                out.append([1 if i in combo else 0 for i in range(len(names))])
    return out if len(out) > 1 else []


def twin(p, q):
    """Two plans that play the same land and the same cards in the same order -- a mode apart."""
    return q is not None and p.get("land") == q.get("land") and \
        [a.get("card") for a in p.get("actions") or []] == [a.get("card") for a in q.get("actions") or []]


def flag_frame(d):
    """Soulfire Eruption's `target` frame (random_damage) is answered with ONE 0/1 FLAG PER LEGAL
    TARGET, not an option index (main.cpp soulfire_chooser: the option list is capped at 256 and
    cannot hold a wide set). The frame is re-emitted with the same decision_index until all
    len(legal_targets) flags are in, so its j-th flag is the pick at stream position
    decision_index + j. Sending heuristic_default (an option index) as every flag targets EVERYTHING,
    your own creatures included."""
    return d.get("type") == "target" and bool(d.get("random_damage")) and isinstance(d.get("legal_targets"), list)


def flags_of(d, targets):
    want = {(t.get("kind"), t.get("index")) for t in targets}
    return [int((t.get("kind"), t.get("index")) in want) for t in d["legal_targets"]]


def default_flags(d):
    """The AI's pick (options[heuristic_default]) as flags."""
    return vpc.engine_flags(d)


def search_flags(d, log, turn, phase, nth):
    """The search's own Soulfire target set as flags, from the `nth` Soulfire reveal of that phase in
    its log ('-> opponent face (2)', '-> you (1)', '-> opponent 1/1 Spirit Token (0)', '-> your Hinata,
    Dawn-Crowned (4)': one per target). None when it cannot be mapped onto this frame's targets."""
    reveals = [a for s in log.get("turns") or [] if s.get("turn") == turn and PHASE_OF.get(s.get("phase")) == phase
               for a in s.get("actions") or []
               if a.get("type") == "REVEAL" and str(a.get("source", "")).startswith("Soulfire Eruption")]
    if nth >= len(reveals):
        return None
    me, opp = d.get("me") or {}, d.get("opponent") or {}
    me, opp = (json.loads(me) if isinstance(me, str) else me), (json.loads(opp) if isinstance(opp, str) else opp)
    name_at = {("me", p.get("idx")): p.get("name") for p in me.get("battlefield") or []}
    name_at.update({("opp", p.get("idx")): p.get("name") for p in opp.get("battlefield") or []})
    lt = d["legal_targets"]
    flags = [0] * len(lt)
    for x in reveals[nth].get("lookedAt") or []:
        to = str(x.get("to", "")).lstrip("→").strip()
        if "(" in to:
            to = to[:to.rindex("(")].strip()
        for i, t in enumerate(lt):
            if flags[i]:
                continue
            lab = t.get("label", "")
            perm = t.get("kind") == "permanent"
            hit = ((to == "opponent face" and lab == "Opponent (face)") or (to == "you" and lab == "You (face)")
                   or (perm and to.startswith("opponent ") and name_at.get(("opp", t.get("index"))) == to[9:])
                   or (perm and to.startswith("your ") and name_at.get(("me", t.get("index"))) == to[5:]))
            if hit:
                flags[i] = 1
                break
        else:
            return None
    return flags


def search_dig_answer(d, log, key, used_reveals):
    """The option of a scry / reorder frame (Preordain, Ponder) that does what the search did: its log's
    REVEAL of the same looked-at cards in this phase names what it kept on top, in order. None when no
    unused REVEAL matches (the replay has drifted, or the search never saw these cards)."""
    looked = collections.Counter(x.get("name") for x in d.get("looked") or [])
    opts = d.get("options") or []
    if not looked or not key or not all(isinstance(o, dict) and "top" in o for o in opts):
        return None
    for si, s in enumerate(log.get("turns") or []):
        if s.get("turn") != key[0] or PHASE_OF.get(s.get("phase")) != key[1]:
            continue
        for ai, a in enumerate(s.get("actions") or []):
            if a.get("type") != "REVEAL" or (si, ai) in used_reveals:
                continue
            la = a.get("lookedAt") or []
            # A scry / reorder logs no disposition per card; a reveal that does ("battlefield (tapped)":
            # Galadriel of Lothlorien's land put, "to hand", "stays on top") is another effect showing the
            # same card, and its empty `kept` read as "bottom it" (Galadriel s5_gi4 T4, 2026-10-27).
            if any(x.get("to") for x in la):
                continue
            if collections.Counter(x.get("cardName") for x in la) != looked:
                continue
            name = {x.get("card"): x.get("cardName") for x in la}
            top = [name.get(n) for n in a.get("kept") or []]
            for o in opts:
                if not o.get("shuffle") and list(o.get("top") or []) == top:
                    used_reveals.add((si, ai))
                    return o.get("index")
    return None



def search_tutor_answer(d, log, key, used_reveals):
    """A tutor's candidate pick (Living Wish, Gamble...) that the search's log names: the REVEAL
    '<source> (searched)' of this phase, kept card. None when there is none."""
    cands = d.get("candidates")
    if not key or not isinstance(cands, list) or not d.get("source"):
        return None
    for si, s in enumerate(log.get("turns") or []):
        if s.get("turn") != key[0] or PHASE_OF.get(s.get("phase")) != key[1]:
            continue
        for ai, a in enumerate(s.get("actions") or []):
            if (a.get("type") != "REVEAL" or (si, ai) in used_reveals
                    or a.get("source") != d["source"] + " (searched)"):
                continue
            name = {x.get("card"): x.get("cardName") for x in a.get("lookedAt") or []}
            kept = [name.get(k) for k in a.get("kept") or []]
            for i, o in enumerate(cands):
                if isinstance(o, dict) and kept and o.get("name") == kept[0]:
                    used_reveals.add((si, ai))
                    return o.get("index", i)
    return None




ROUTE = "Combo Off"   # an engine go-off route (TurnSolver ComboRoute): ONE logged action for a whole chain
LOOK = 8              # boards looked one segment past, and plans probed from each


def attachments(b):
    """{(Equipment / Aura name, host card number)} on a log board."""
    return {(x.get("cardName"), x.get("attachedTo")) for x in (b or {}).get("battlefield") or []
            if x.get("attachedTo")}


def equip_score(p, want, d):
    """How faithful a plan's equips are to the search's end-of-phase attachments `want`: +1 per equip the
    search's board has, -1 per one it does not. `equip all free` moves the frame's free pieces onto its
    host (the frame lists them in free_equips)."""
    sc = 0
    for a in p.get("actions") or []:
        if a.get("verb") == "equip":
            sc += 1 if (a.get("card"), a.get("equip_host")) in want else -1
        elif a.get("verb") == "equipallfree":
            fe = next((f for f in d.get("free_equips") or [] if f.get("host") == a.get("host")), {})
            for pc in fe.get("pieces") or []:
                sc += 1 if (pc.get("name"), a.get("host")) in want else -1
    return sc


def plan_ok(p, used):
    """A plan the search could have been playing: its land drop (if any) is one the search made this
    phase, and every card it uses is one the search used."""
    if p.get("land") and plan_land(p) not in used["lands"] and p["land"] not in used["lands"]:
        return False
    cards = {a.get("card") for a in p.get("actions") or [] if a.get("card")}
    # "X + Remand on it" casts Remand too (back to hand, draw a card): only the search's if it did
    # (Hinata2 s4_gi3, 2026-10-26).
    cards |= {a.get("remand_by") for a in p.get("actions") or [] if a.get("remand_by")}
    return bool(cards or p.get("land")) and cards <= used["cards"]


def wrong_x(a, xs):
    """A cast at an X the search did not use for that card: not the search's action. A plan's X is fixed
    when the plan is enumerated, from the mana available THEN, so the search's bigger X can need an
    earlier segment first (SelesnyaLifegain s15_gi14, 2026-10-25: Feed the Clan + Wellwisher gain the
    life Accomplished Alchemist taps for, and only the next segment offers Genesis Wave X=14)."""
    if a.get("activate") or not xs.get(a.get("card")):
        return False
    return (a.get("x") or 0) not in xs[a["card"]]          # a plan omits "x" at X=0


def content(p, xs=None):
    """The search-log keys the plan plays. An equip is keyed apart from a cast (plan_key): "equip Kite Shield"
    counted as a third Kite Shield CAST, so a plan casting two and equipping one matched the search's three
    casts and left a Kite Shield on Puresteel Paladin that the search's board does not have, one Paladin
    draw short (KittyEquipment v2 s13_gi12 T2)."""
    c = collections.Counter(plan_key(a) for a in plays(p)     # "drops" cannot be cast (burn s8_gi7)
                            if a.get("card") and not wrong_x(a, xs or {}))
    c.update(a["remand_by"] for a in p.get("actions") or [] if a.get("remand_by"))
    if p.get("land"):
        c["land:" + plan_land(p)] += 1
    return c


def found_match(p, found):
    """How many of the plan's tutor targets, in the plan's order, are the cards the search's tutors
    fetched NEXT this phase (`found`: (tutor, card) pairs, not yet fetched, in log order)."""
    n = 0
    for a in plays(p):
        if a.get("tutor_target"):
            if n < len(found) and found[n] == (a.get("card"), a["tutor_target"]):
                n += 1
            else:
                break
    return n


def in_order(p, seq, xs=None):
    """How many of the plan's actions, in the plan's order, are the search's NEXT unplayed actions this
    phase (`seq`: its non-land named actions in log order, those already played removed)."""
    n = 0
    for a in plays(p):
        if not a.get("card") or wrong_x(a, xs or {}):
            continue
        if n < len(seq) and seq[n] == plan_key(a):
            n += 1
        else:
            break
    return n


def land_misplaced(p, oseq):
    """Does the plan play the search's land drop at a different point among the search's actions than the
    search did (`oseq`: its remaining named actions this phase, lands included, in log order)? The search
    cast Gold Rush and Luxurious Libation, THEN played Gruul Turf -- a bounce land, so playing it first
    returns a land before it has paid (Mirrorwing v2 s4_gi3 T3, 2026-10-27)."""
    if not p.get("land") or not oseq:
        return False
    tok = "land:" + plan_land(p)
    if tok not in oseq:
        return False
    before_search = sum(1 for t in oseq[:oseq.index(tok)] if not t.startswith("land:"))
    ps = plan_seq(p)
    before_plan = sum(1 for t in ps[:ps.index(tok)] if t in oseq) if tok in ps else 0
    return before_plan != before_search


def progress(p, rest, xs=None, found=(), seq=(), oseq=None):
    """(how many of the search's next actions this phase the plan plays IN THE SEARCH'S ORDER, how many
    of its still-unplayed actions it plays at all, minus its casts at an X the search did not use) --
    compared as a tuple. A tutor target the search fetched next counts as an action in both.

    ORDER first: a plan that plays more of the search's cards out of order can strand the rest. The
    search cracked a Clue for the Peregrine Drake before casting Cloud of Faeries and Eldrazi
    Displacer; "Cloud + Displacer" plays two of its cards to the Clue plan's one and spends the mana the
    Drake needed (EldraziDisplacerFlicker s15_gi14 T5, s20_gi19 T3, 2026-10-26). The TUTOR TARGET counts
    because the board after the cast cannot show it: Living Wish -> Adarkar Wastes leaves the land in
    hand until a later segment plays it, so a probe prefers whichever target the search's end-of-phase
    HAND holds (Cloud of Faeries) and the land drop is lost (s7_gi6 T3)."""
    tut = found_match(p, list(found))
    land = 1 if p.get("land") and rest["land:" + plan_land(p)] else 0
    # The land drop at the wrong point is worse than no land drop: no credit, and it ranks below the plan
    # that leaves the land for its own segment (the board probe would still prefer it -- the land is on
    # the battlefield the search ends with -- but what it costs, the bounced land's mana, shows only later).
    off = land if land_misplaced(p, oseq) else 0
    return (in_order(p, list(seq), xs) + land - 2 * off + tut,
            sum((content(p, xs) & rest).values()) - off + tut,
            -sum(wrong_x(a, xs or {}) for a in p.get("actions") or []),
            # Last: the fewest actions BEYOND the search's (an extra tap-for-life Wellwisher activation
            # probes closer only because its life stands in for a Feed the Clan still to come, then
            # leaves Wellwisher tapped for the search's swing: SelesnyaLifegain s15_gi14 T5,
            # 2026-10-26). Unnamed activations (Land's Edge discards) are hill-climbed once the named
            # actions are all played, where this filter no longer applies.
            -sum((content(p, xs) - rest).values()))


# ---- side channels ------------------------------------------------------------------------------

def side_before(side, turn, ordinal):
    """The reference's keyed side-channel args, keeping only the entries for turns before `turn`
    (main ordinals before `ordinal`): the human's pins must steer the human's turns, never the
    search's. Value-less recording-rule flags (--legacy-*) are kept -- the whole game is played, and
    then re-recorded, under the rules the human's turns were recorded under."""
    turn_keyed = {"--firebreathe": ",", "--storage-hold": ",", "--jitte": ",",
                  "--force-attackers": ";", "--tap-pref": ";"}
    ord_keyed = {"--cast-order": ";", "--full-enum": ";"}
    out, i = [], 0
    while i < len(side):
        a = side[i]
        if a.startswith(LEGACY_PREFIX):
            out.append(a)
            i += 1
            continue
        if i + 1 >= len(side):
            raise Refused("unrecognised side-channel arg %r" % a)
        val = side[i + 1]
        if a in turn_keyed or a in ord_keyed:
            sep = (turn_keyed.get(a) or ord_keyed.get(a))
            lim = turn if a in turn_keyed else ordinal
            keep = [e for e in val.split(sep) if e and int(e.split(":", 1)[0]) < lim]
            if keep:
                out += [a, sep.join(keep)]
        else:
            raise Refused("side-channel %s has no known key, cannot split it at the hand-off" % a)
        i += 2
    return out


def search_attackers(log, from_turn):
    """turn -> the attacker names of the search's combat, for every turn from `from_turn` on whose combat
    dealt damage, except the winning one. The log does not name attackers, so they are the non-land
    permanents that went from untapped (after main 1) to tapped (after combat); a vigilance attacker is
    invisible, and a turn where nothing is seen is left unpinned. The attack is not a decision frame in
    claude-play -- the engine declares it -- so without the pin the replay attacks with its own choice
    (Bruna s14_gi13, Creature_Giving s2_gi1 / s6_gi5, SelesnyaLifegain s15_gi14 T5, 2026-10-26). The
    winning turn is left to the engine because a recording carries a combat only through a later
    frame's events (viewer_protocol_check.recorded_attackers), and a lethal swing has none."""
    res = log.get("result") or {}
    win = res.get("turn") if res.get("winner") == "player" else None
    out, prev = {}, None
    for st in log.get("turns") or []:
        t, b = st.get("turn") or 0, st.get("boardAfter") or {}
        dealt = any(a.get("type") == "ATTACK" and (a.get("damage") or 0) > 0 for a in st.get("actions") or [])
        if st.get("phase") == "COMBAT" and dealt and t >= from_turn and t != win and prev is not None:
            was = {x.get("card"): x.get("tapped") for x in prev.get("battlefield") or []}
            names = [x.get("cardName") for x in b.get("battlefield") or []
                     if not x.get("isLand") and x.get("tapped") and was.get(x.get("card")) is False]
            if names:
                out[t] = names
        prev = b
    return out


def with_attackers(extra, pins):
    """`extra` with `pins` (turn -> names) merged into its --force-attackers (the human's turns keep theirs)."""
    if not pins:
        return list(extra)
    out, have = [], {}
    i = 0
    while i < len(extra):
        if extra[i] == "--force-attackers" and i + 1 < len(extra):
            for e in extra[i + 1].split(";"):
                if e:
                    t, _, names = e.partition(":")
                    have[int(t)] = names
            i += 2
            continue
        out.append(extra[i])
        i += 1
    for t, names in pins.items():
        have.setdefault(t, "|".join(names))
    return out + ["--force-attackers", ";".join("%d:%s" % (t, have[t]) for t in sorted(have))]



def with_jitte(extra, log, from_turn, max_turns):
    """`extra` with `--jitte t:-1` ("spend the counters as they arrive") on every turn from `from_turn`
    on, when the search's game holds Umezawa's Jitte; the human's turns keep their own answers. The
    search spends greedily, including the counters a double striker's first strike earns, while human
    play with no answer BANKS them, so without the pin the replayed swing falls short (KittyEquipment
    v1 s15_gi14 T6: the search's 20 into 18 life replayed as 16, 2026-10-27). Turns where no Jitte
    carrier attacks never consult the answer, and write nothing into the reference."""
    if "Umezawa's Jitte" not in card_names(log).values():
        return list(extra)
    out, have = [], {}
    i = 0
    while i < len(extra):
        if extra[i] == "--jitte" and i + 1 < len(extra):
            for e in extra[i + 1].split(","):
                if e:
                    t, _, n = e.partition(":")
                    have[int(t)] = n
            i += 2
            continue
        out.append(extra[i])
        i += 1
    for t in range(from_turn, max_turns + 1):
        have.setdefault(t, "-1")
    return out + ["--jitte", ",".join("%d:%s" % (t, have[t]) for t in sorted(have))]

# ---- the walk -----------------------------------------------------------------------------------

def Driver(c, extra):
    """rr.Driver at the protocol checker's plan-list valve (the bound every promotion is checked at)."""
    return rr.Driver(c, extra, c.get("valve") or vpc.VALVE_POSITIONS)


def probe_on(c, extra, picks, want_win):
    """Play `picks`, then engine defaults through every non-main-phase frame, and stop at the next
    main-phase frame or the result: the board a candidate plan REALLY leaves, after its own follow-up
    choices (a Land's Edge plan's discards, a tutor's pick) have resolved. -> (kind, frame, won_like_log)."""
    s = Driver(c, extra)
    try:
        for _ in range(2000):
            kind, d = s.read()
            if kind == "result":
                return kind, d, bool(d.get("won")) and d.get("win_turn") == want_win
            if kind == "eof":
                return kind, None, False
            if len(s.picks) < len(picks):
                s.send(picks[len(s.picks)])
            elif s.pending():
                pass
            elif d.get("type") == "main_phase":
                return kind, d, False
            else:
                s.answer(default_answer(d))
        return "eof", None, False
    finally:
        s.close()


def probe_next_turn(c, extra, picks, turn):
    """Play `picks`, then engine defaults at every other frame and PASS at every main phase, up to the
    first main-phase frame of a turn after `turn`. -> that frame, or None. The tie-break for answers
    whose boards look the same at the next main phase: a card tutored to the TOP of the library (Worldly
    Tutor) is invisible until it is drawn, so only the next turn's hand tells the answers apart."""
    s = Driver(c, extra)
    try:
        for _ in range(2000):
            kind, d = s.read()
            if kind != "decision":
                return None
            if len(s.picks) < len(picks):
                s.send(picks[len(s.picks)])
            elif s.pending():
                pass
            elif d.get("type") == "main_phase":
                if (d.get("turn") or 0) > turn:
                    return d
                s.send(-1)
            else:
                s.answer(default_answer(d))
        return None
    finally:
        s.close()


def probe(c, extra, picks):
    """The frame (or result) after `picks`, from a fresh stateless replay."""
    rc, out = vpc.replay(c["deck"], c["prof"], c["seed"], c["gi"], picks, c["force"], extra, c["mt"],
                         valve=c.get("valve") or vpc.VALVE_POSITIONS)
    m = vpc.DEC_RE.search(out)
    if rc == 70 and m:
        return "decision", json.loads(m.group(1))
    r = hunt.result_of(out)
    return ("result", r) if isinstance(r, dict) else ("eof", out[-400:])


def walk(c, prefix, extra, log, from_turn):
    """-> (the full pick stream that plays `prefix` and then the search's line, notes). Raises Refused.

    Among the plans that use only cards the search used in that phase: while some of the search's
    named actions are still unplayed, the plan that plays the most of them (ties: the one whose probed
    result is closest to the search's END-OF-PHASE board -- battlefield, hand, opponent life); after
    that, repeat activations are hill-climbed on that distance, and the phase passes when nothing gets
    closer. A plan that reaches the search's own win is taken at once. Progress has to come first: a
    pure distance climb stalls in a local minimum (Treasure Hunt draws fifteen lands, moving the hand
    AWAY from the board Land's Edge later reaches). Card names alone cannot drive this: the
    autonomous log does not name every activation (21 Land's Edge discards are 21 two-damage lines),
    and the viewer splits one search phase into several segments (a land and a Treasure Hunt, then
    the Land's Edge it drew). Exact identity with the search's game is NOT required -- which land a
    discard picks is immaterial -- the gate is the outcome: the same win on the same turn."""
    names = phase_names(log, from_turn)
    bd = boards(log)
    res = log.get("result") or {}
    want_win = res.get("turn") if res.get("winner") == "player" else None
    s = Driver(c, extra)
    checked, notes, first_div, cur_key = set(), [], None, None
    rest = {k: collections.Counter(v["count"]) for k, v in names.items()}
    found = {k: list(v["found"]) for k, v in names.items()}    # tutor fetches not yet made, in order
    seqs = {k: list(v["seq"]) for k, v in names.items()}       # named actions not yet played, in order
    rest_oseq = {k: list(v["oseq"]) for k, v in names.items()}  # the same with land drops, for order_cost
    claimed = []                                               # tutor targets of the plan just taken
    soulfire_seen, used_reveals = {}, set()
    marks = {}            # turn -> len(picks) at its first main-phase frame (where rescue() restarts)
    try:
        for _ in range(5000):
            kind, d = s.read()
            if kind == "result":
                won = d.get("won")
                if not won or d.get("win_turn") != want_win:
                    err = Refused("the replayed line ends %s, the search's game %s%s" % (
                        "T%s" % d.get("win_turn") if won else "without a win",
                        "T%s" % want_win if want_win else "without a win",
                        (" -- it first drifted from the search's game " + first_div) if first_div else ""))
                    err.picks, err.marks = list(s.picks), dict(marks)
                    raise err
                return list(s.picks), notes
            if kind == "eof":
                raise Refused("the replay stopped without a result: %s" % str(d)[-300:])
            if len(s.picks) < len(prefix):
                s.send(prefix[len(s.picks)])
                continue
            if s.pending():
                continue
            if d.get("type") != "main_phase" and flag_frame(d):
                # Soulfire: the search's own target set when its log names it, else the AI's pick.
                nth = soulfire_seen[cur_key] = soulfire_seen.get(cur_key, -1) + 1
                vec = search_flags(d, log, cur_key[0], cur_key[1], nth) if cur_key else None
                say("   soulfire targets %s (%s)" % (vec or default_flags(d), "search's" if vec else "AI default"))
                s.answer(vec or default_flags(d))
                continue
            if d.get("type") != "main_phase":
                dig = search_dig_answer(d, log, cur_key, used_reveals)
                if dig is not None:
                    say("   %s: the search's own order -> option %s" % (d.get("type"), dig))
                    s.answer([dig])
                    continue
                # A tutor pick answers BY NAME with what the search fetched: while the card sits in hand
                # every answer probes alike, and the search may play it only a segment later.
                cn = [x.get("name") if isinstance(x, dict) else None for x in d.get("candidates") or []]
                fq = found.get(cur_key) or []
                head = claimed[0] if claimed else (fq[0] if fq else None)
                # ...made by THIS tutor: another tutor's frame (Natural Order's, while the queue holds
                # Worldly Tutor's find) probes its options instead.
                want = head[1] if head is not None and (not d.get("source")
                                                        or str(d["source"]).startswith(head[0])) else None
                if want is not None and want in cn and not is_flags(d):
                    (claimed if claimed else fq).pop(0)
                    o = d["candidates"][cn.index(want)].get("index", cn.index(want))
                    say("   %s: the search fetched %s -> option %s" % (d.get("type"), want, o))
                    s.answer([o])
                    continue
                picks_ = answers(d)
                if not picks_:
                    s.answer(default_answer(d))
                    continue
                # A target, a tutor pick, a scry, a put-into-play: the answer whose board, once play
                # reaches the next main-phase frame, is closest to the search's at that point.
                best, ties, lost = (10 ** 9, picks_[0]), [], {}
                for o in picks_[:MAX_PROBES]:
                    k2, d2, win = probe_on(c, extra, list(s.picks) + o, want_win)
                    if win:
                        best, ties = (-1, o), []
                        break
                    if k2 != "decision" or d2 is None:
                        continue
                    k2key = (d2.get("turn"), d2.get("phase"))
                    ref = bd.get(k2key) or (None, None)
                    tgt = log_sig(ref[1] if k2key == cur_key else ref[0])
                    if tgt is None:
                        continue
                    sg = projected(d2, rest.get(cur_key) or collections.Counter(), tgt) if k2key == cur_key \
                        else frame_sig(d2)
                    dist = sig_diff(sg, tgt)[0]
                    lost[tuple(o)] = sg[4] if len(sg) > 4 else 0
                    say("   %s option %s -> %d" % (d.get("type"), o, dist))
                    if dist < best[0]:
                        best, ties = (dist, o), [o]
                    elif dist == best[0]:
                        ties.append(o)
                # Among equally close answers, one that loses a card the search still plays this phase is
                # out: Brainstorm putting back the Remand the search casts next ties with putting back a
                # Curiosity (the other only swaps which Curiosity copy is in hand), and the next turn's
                # board cannot tell them apart (NivMizzetCombo s4_gi3 T3, 2026-10-27).
                if len(ties) > 1:
                    keep = min(lost.get(tuple(o), 0) for o in ties)
                    ties = [o for o in ties if lost.get(tuple(o), 0) == keep]
                    best = (best[0], ties[0])
                # Answers that leave the same visible board (a card tutored to the top of the library)
                # are told apart at the start of the next turn, after the draw.
                if len(ties) > 1:
                    t = d.get("turn") or 0
                    deep = (10 ** 9, ties[0])
                    for o in ties:
                        d3 = probe_next_turn(c, extra, list(s.picks) + o, t)
                        tgt = d3 and log_sig((bd.get((d3.get("turn"), d3.get("phase"))) or (None, None))[0])
                        if tgt is None:
                            continue
                        dist = sig_diff(frame_sig(d3), tgt)[0]
                        say("   %s option %s, at T%s -> %d" % (d.get("type"), o, d3.get("turn"), dist))
                        if dist < deep[0]:
                            deep = (dist, o)
                    best = (best[0], deep[1])
                s.answer(best[1])
                continue
            key = (d.get("turn"), d.get("phase"))
            marks.setdefault(d.get("turn"), len(s.picks))
            cur_key = key
            if key not in checked:
                checked.add(key)
                before = log_sig((bd.get(key) or (None, None))[0])
                if before is not None and first_div is None:
                    dist, why = sig_diff(frame_sig(d), before)
                    if dist:
                        first_div = "at the start of T%s %s (%s)" % (key[0], key[1], why)
                        notes.append("drifted " + first_div)
            used = names.get(key) or {"lands": set(), "cards": set(), "count": collections.Counter()}
            b0, b1 = bd.get(key) or (None, None)
            target = log_sig(b1)
            if target is None:
                s.send(-1)
                continue
            # A permanent the search had in play during the phase can be ACTIVATED even when the log
            # never names it (a cascaded-into Land's Edge discarding lands shows only as damage lines).
            used = dict(used, cards=used["cards"] | {x.get("cardName") for b in (b0, b1) if b
                                                     for x in b.get("battlefield") or []})
            # A go-off ROUTE logs one action for its whole chain, the land drop included: a land on the
            # search's end-of-phase battlefield that was not on it before is a drop the search made
            # (KittyEquipment v2 s6_gi5 T3: the route drew Ancient Den and played it).
            route = ROUTE in used["cards"]
            if route and b1:
                lands = lambda b: collections.Counter(x.get("cardName") for x in (b or {}).get("battlefield") or []
                                                      if x.get("isLand"))
                used = dict(used, lands=set(used["lands"]) | set(lands(b1) - lands(b0)))
            # On the search's WIN turn the viewer's verified COMBO OFF is the search's own go-off. The
            # autonomous log shows the loop only as untaps and draws (no 'Combo Off' marker when the search
            # stepped through it), so no per-card plan matches it and the distance -- dominated by a
            # drawn-out library -- cannot steer there (EldraziDisplacerFlicker s4_gi3 T5, s7_gi6 T4,
            # 2026-10-26). Taken only when its probe reaches that very win.
            if want_win is not None and key[0] == want_win:
                co = next((p for p in d.get("plans") or [] if p.get("combo_off")
                           and probe_on(c, extra, list(s.picks) + [p["index"]], want_win)[2]), None)
                if co is not None:
                    say("T%s %s: take [%d] COMBO OFF (reaches the win)" % (key[0], key[1], co["index"]))
                    s.send(co["index"])
                    continue
            cands = [p for p in d.get("plans") or [] if plan_ok(p, used)]
            if not cands:
                s.send(-1)
                continue
            left = rest.setdefault(key, collections.Counter())
            # ...and the route is a plan only where the frame OFFERS it (the viewer's COMBO OFF entry). Where
            # it does not, counting it as still unplayed forbids passing for the rest of the phase, so every
            # segment takes SOME plan however far it moves from the search's board (KittyEquipment v2
            # s6_gi5 T3: equip Spidersilk Net 7 -> 9, then equip Kite Shield -> 11).
            if left[ROUTE] and not any(a.get("card") == ROUTE for p in cands for a in p.get("actions") or []):
                left = rest[key] = left - collections.Counter({ROUTE: left[ROUTE]})
                for q in (seqs.get(key), rest_oseq.get(key)):
                    while q and ROUTE in q:
                        q.remove(ROUTE)
            want = attachments(b1)
            fq, sq = found.setdefault(key, []), seqs.setdefault(key, [])
            claimed = []
            if +left or fq:
                xs = used.get("xs") or {}
                ol = rest_oseq.get(key) or []
                top = max(progress(p, left, xs, fq, sq, ol) for p in cands)
                if top[0] > 0 or top[1] > 0:
                    cands = [p for p in cands if progress(p, left, xs, fq, sq, ol) == top]
            # Probe the plans that do least BEYOND the search's actions first, so a tie (and the stop at
            # distance 0) goes to the cleaner one: an extra tap-for-life Wellwisher activation leaves the
            # same names, hand and opponent life, but the tapped Wellwisher then misses the search's
            # swing (SelesnyaLifegain s15_gi14 T5, 2026-10-26). Stable, so the menu order breaks ties.
            # And before that, the plans in the search's own ORDER, lands at their position (order_cost).
            oseq = rest_oseq.setdefault(key, [])
            can = collections.Counter()
            for p in cands:
                can |= content(p, used.get("xs") or {})
            oseq_here = [t for t in oseq if can[t]]
            # Equips are content no NAME shows, so the plans whose equips match the search's board come
            # first: MAX_PROBES cuts the list (KittyEquipment v2 s6_gi5 T3: 157 plans, the search's
            # "Colossus Hammer, Shadowspear, equip Colossus Hammer -> Puresteel Paladin" was #35).
            cands.sort(key=lambda p: (-equip_score(p, want, d), order_cost(p, oseq_here, used.get("xs") or {}),
                                      sum((content(p, used.get("xs") or {}) - left).values())))
            # The plan must beat PASSING (`here`) only once the search's named actions are all played.
            here = sig_diff(frame_sig(d), target)[0]
            if here == 0 and not +left:
                s.send(-1)                       # already the search's board: nothing to probe
                continue
            best = (here if not +left else 10 ** 9, None)
            say("T%s %s: distance %d to the search's board, %d candidate(s)" % (key[0], key[1], here, len(cands)))
            dists = []
            for p in cands[:MAX_PROBES]:
                if best[0] == 0 and not twin(p, best[1]):
                    continue                     # the search's board is reached: only its twins remain
                k2, d2, win = probe_on(c, extra, list(s.picks) + [p["index"]], want_win)
                if win:
                    best = (-1, p)
                    break
                if d2 is None or k2 != "decision":
                    continue
                # A plan that does nothing (the same frame comes back): after Irencrag Feat's lock the
                # viewer still offers thousands of casts (Hinata2 s9_gi8 T5, 2026-10-26).
                if (d2.get("type") == "main_phase" and (d2.get("turn"), d2.get("phase")) == key
                        and frame_sig(d2) == frame_sig(d)
                        and [q.get("summary") for q in d2.get("plans") or []]
                        == [q.get("summary") for q in d.get("plans") or []]):
                    say("   [%d] %-70s -> no-op" % (p["index"], p.get("summary", "")[:70]))
                    continue
                if (d2.get("turn"), d2.get("phase")) == key:      # the plan's next segment: mid-phase
                    sg = projected(d2, left - content(p, used.get("xs") or {}), target)
                else:
                    sg = frame_sig(d2)
                dist = sig_diff(sg, target)[0]
                dists.append((dist, p, d2))
                say("   [%d] %-70s -> %d" % (p["index"], p.get("summary", "")[:70], dist))
                if dist < best[0]:
                    best = (dist, p)
            # TWINS -- the same land and cards, a MODE apart that the board does not show yet: Mariposa
            # Military Base entering tapped for two rad counters mills two at the NEXT precombat main,
            # which moves every later draw (EldraziDisplacerFlicker s13_gi12 T2, 2026-10-26). They are
            # told apart at the next turn's first main phase: the visible board against the search's at
            # the start of that phase, the GRAVEYARD SIZE against its end (the frame follows the
            # beginning-of-main mill, the log's start-of-phase board predates it; whatever the search
            # then cast counts the same for every twin). One twin per distinct summary.
            tw, seen = [], set()
            for dd, p, _ in dists:
                if best[0] >= 0 and dd == best[0] and twin(p, best[1]) and p.get("summary") not in seen:
                    seen.add(p.get("summary"))
                    tw.append(p)
            if len(tw) > 1:
                deep = (10 ** 9, best[1])
                for p in tw:
                    d3 = probe_next_turn(c, extra, list(s.picks) + [p["index"]], key[0])
                    b3, a3 = (d3 and bd.get((d3.get("turn"), d3.get("phase")))) or (None, None)
                    if not b3 or not a3:
                        continue
                    dd = sig_diff(frame_sig(d3), log_sig(b3))[0] + abs(gy_size(d3["me"]) - len(a3.get("graveyard") or []))
                    say("   [%d] twin, at T%s -> %d" % (p["index"], d3.get("turn"), dd))
                    if dd < deep[0]:
                        deep = (dd, p)
                best = (best[0], deep[1])
            # ONE SEGMENT ON. The viewer cuts a phase at every draw, so a cheap first cast pays off only in
            # the next segment: KittyEquipment v1 s12_gi11 T3, Colossus Hammer first (it draws Sol Ring and
            # leaves the mana for it; Grafted Wargear then makes metalcraft and both Hammers equip free) is 9
            # from the search's board, Grafted Wargear first (all three Plains, nothing after) is 7. Boards
            # that tie are always looked past; in a ROUTE phase every board is, because the route's one log
            # line names neither its casts nor their order, so only the myopic distance is left to steer.
            if best[0] > 0:
                look, seen = [], set()
                for dd, p, d2 in dists:
                    if route or (best[1] is not None and dd == best[0]):
                        sg = frame_sig(d2)
                        fk = (d2.get("turn"), d2.get("phase"), repr(sorted(sg[0].items())),
                              repr(sorted(sg[1].items())), sg[2], sg[3])
                        if fk not in seen:
                            seen.add(fk)
                            look.append((dd, p, d2))
                # Bounded: the LOOK closest boards, each probed LOOK plans deep (a Puresteel frame offers 1,300
                # plans and every probe replays the game).
                look.sort(key=lambda v: v[0])
                look = look[:LOOK]
                if len(look) > 1 or (route and look):
                    deep = best
                    for dd, p, d2 in look:
                        sub = dd
                        if (d2.get("turn"), d2.get("phase")) == key and d2.get("type") == "main_phase":
                            nxt = [q for q in d2.get("plans") or [] if plan_ok(q, used)]
                            nxt.sort(key=lambda q: -equip_score(q, want, d2))
                            for q in nxt[:LOOK]:
                                k3, d3, win3 = probe_on(c, extra, list(s.picks) + [p["index"], q["index"]], want_win)
                                if win3:
                                    sub = -1
                                    break
                                if k3 == "decision" and d3 is not None:
                                    sg3 = (projected(d3, left - content(p, used.get("xs") or {})
                                                     - content(q, used.get("xs") or {}), target)
                                           if (d3.get("turn"), d3.get("phase")) == key else frame_sig(d3))
                                    sub = min(sub, sig_diff(sg3, target)[0])
                        say("   [%d] one segment on -> %d" % (p["index"], sub))
                        if sub < deep[0]:
                            deep = (sub, p)
                    best = deep
            if best[1] is None:
                say("   pass")
                s.send(-1)
                continue
            say("   take [%d]%s" % (best[1]["index"], " (reaches the win)" if best[0] < 0 else ""))
            rest[key] = left - content(best[1], used.get("xs") or {})
            for i in sorted(seq_match(best[1], oseq, used.get("xs") or {}), reverse=True):
                del oseq[i]
            for a in best[1].get("actions") or []:
                if a.get("card") and plan_key(a) in sq and not wrong_x(a, used.get("xs") or {}):
                    sq.remove(plan_key(a))
            m = found_match(best[1], fq)
            claimed = fq[:m]
            del fq[:m]
            if len(cands) > MAX_PROBES:
                notes.append("T%s %s: probed %d of %d candidate plans" % (key[0], key[1], MAX_PROBES, len(cands)))
            s.send(best[1]["index"])
        raise Refused("walked 5000 frames without a result")
    finally:
        s.close()


RESCUE_NODES = 160    # main-phase frames rescue() may expand


def rescue(c, extra, start, log, want):
    """-> a pick stream that wins on turn `want`, or None. The guided walk commits one plan per frame, and
    a viewer plan can execute in a different order from the one it lists (Hinata2 s9_gi8 T5: "Reality
    Spasm, Soulfire Eruption, Irencrag Feat" casts Irencrag BEFORE Soulfire, whose cast then spends
    Irencrag's one remaining spell, while the search cast Irencrag after Soulfire's exile) or pay a segment
    from sources the search kept for later (s1_gi0 T6: Cascade Bluffs, the second Reality Spasm's {U}{U}).
    So when the walk misses the win, the win turn alone is searched depth-first from its first main-phase
    frame: at each main frame the plans that use only the search's cards, in the walk's own progress order,
    then passing; every other frame answered as the walk answers it. The gate is unchanged: the result
    must still win on exactly the search's turn and round-trip."""
    names, bd = phase_names(log, 1), boards(log)
    seen, budget = set(), [RESCUE_NODES]

    def advance(picks):
        s = Driver(c, extra)
        used_rev, sf, cur = set(), {}, None
        try:
            while True:
                kind, d = s.read()
                if kind != "decision":
                    return kind, d, list(s.picks)
                pre = len(s.picks) < len(picks)
                if d.get("type") == "main_phase":
                    cur = (d.get("turn"), d.get("phase"))
                    if not pre:
                        return kind, d, list(s.picks)
                    s.send(picks[len(s.picks)])
                    continue
                if flag_frame(d):
                    j = len(s.picks) - int(d.get("decision_index") or 0)
                    if j == 0:
                        sf[cur] = sf.get(cur, -1) + 1
                        if not pre:          # a node: the search's target set, then the AI's
                            vecs = [v for v in ((search_flags(d, log, cur[0], cur[1], sf[cur]) if cur else None),
                                                default_flags(d)) if v]
                            return "flags", [v for i, v in enumerate(vecs) if v not in vecs[:i]], list(s.picks)
                    s.send(picks[len(s.picks)] if pre else 0)
                    continue
                if pre:
                    s.send(picks[len(s.picks)])
                    continue
                if s.pending():
                    continue
                ans = search_dig_answer(d, log, cur, used_rev)
                if ans is None:
                    ans = search_tutor_answer(d, log, cur, used_rev)
                s.answer([ans] if ans is not None else default_answer(d))
        finally:
            s.close()

    def dfs(picks, taken):
        if budget[0] <= 0:
            return None
        budget[0] -= 1
        kind, d, picks = advance(picks)
        if kind == "result":
            return picks if d.get("won") and d.get("win_turn") == want else None
        if kind == "flags":
            for v in d:
                got = dfs(picks + v, taken)
                if got:
                    return got
            return None
        if kind != "decision" or (d.get("turn") or 0) > want:
            return None
        me = d.get("me") or {}
        me = json.loads(me) if isinstance(me, str) else me
        sig = (d.get("turn"), d.get("phase"), repr(frame_sig(d)), me.get("life"), repr(me.get("floating_mana")),
               repr(sorted((p.get("num"), bool(p.get("tapped"))) for p in me.get("battlefield") or [])),
               repr(sorted((h.get("num"), bool(h.get("is_staged"))) for h in me.get("hand") or [])))
        if sig in seen:
            return None
        seen.add(sig)
        key = (d.get("turn"), d.get("phase"))
        used = names.get(key) or {"lands": set(), "cards": set(), "count": collections.Counter(), "seq": []}
        b0, b1 = bd.get(key) or (None, None)
        used = dict(used, cards=used["cards"] | {x.get("cardName") for b in (b0, b1) if b
                                                 for x in b.get("battlefield") or []})
        xs = used.get("xs") or {}
        done = taken.get(key, collections.Counter())
        left = collections.Counter(used["count"]) - done
        ol = list(used.get("seq") or [])
        for n in done.elements():
            if n in ol:
                ol.remove(n)
        # Counted, not just named: a plan may not play more copies than the search still has to play
        # (a second Soulfire Eruption where the search cast one).
        cands = [p for p in d.get("plans") or [] if plan_ok(p, used) and not (content(p, xs) - left)]
        cands.sort(key=lambda p: progress(p, left, xs, (), ol), reverse=True)
        for p in cands:
            t2 = dict(taken)
            t2[key] = done + content(p, xs)
            got = dfs(picks + [p["index"]], t2)
            if got:
                return got
        return dfs(picks + [-1], taken)

    got = dfs(list(start), {})
    say("   rescue: %s after %d node(s)" % ("found" if got else "nothing", RESCUE_NODES - budget[0]))
    return got




def write_trace(c, extra, picks, outdir):
    """Re-run the full pick stream with --log-dir: the ENGINE writes the reference file."""
    shutil.rmtree(outdir, ignore_errors=True)
    os.makedirs(outdir)
    args = [vpc.MTG, c["deck"], "--claude-play", "--seed", str(c["seed"]), "--game-index", str(c["gi"]),
            "--max-turns", str(c["mt"]), "--depth", "0", "--profile", c["prof"],
            "--choices", ",".join(str(p) for p in picks), "--log-dir", outdir]
    if c["force"] is not None:
        args += ["--force-mulligan", c["force"]]
    args += extra
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0",
               MTG_VIEWER_PLAN_CAP_POSITIONS=str(c.get("valve") or vpc.VALVE_POSITIONS))
    p = subprocess.run(vpc.capped(args), capture_output=True, text=True, cwd=ROOT, env=env)
    path = os.path.join(outdir, "claude_s%d_gi%d.json" % (c["seed"], c["gi"]))
    if p.returncode != 0 or not os.path.exists(path):
        raise Refused("writing the trace failed (rc=%d): %s" % (p.returncode, p.stderr[-300:]))
    return path


def roundtrip(cand_path, ref_dir):
    """check_reference on the candidate, resolved against the deck of `ref_dir` (references/<dir>)."""
    rel = os.path.dirname(os.path.relpath(cand_path, "references"))
    vpc.DECKS[rel] = vpc.DECKS[ref_dir]
    ok, kind, detail = vpc.check_reference(cand_path)
    if not ok or kind != "ok":
        raise Refused("the promoted line does not replay as a literal reference (%s: %s)" % (kind, detail))


# ---- promote ------------------------------------------------------------------------------------

def promote(key, source, apply_it, note=""):
    os.chdir(ROOT)
    idx = {e["key"]: e for e in rr.load_index()["games"]}
    if key not in idx:
        raise Refused("'%s' is not in the review index" % key)
    e = idx[key]
    gdir = os.path.join(rr.OUT, e["dir"])
    log_path = os.path.join(gdir, source)
    if not os.path.exists(log_path):
        raise Refused("no %s for this game" % source)
    log = json.load(open(log_path))
    if key.startswith("suboptimal/"):
        raise Refused("references/suboptimal/ games are not promoted (they are viewer-limit notes)")
    ref_path = os.path.join("references", key)
    ref_dir = os.path.dirname(key)

    c = {}
    ok, kind, detail = vpc.check_reference(ref_path, collect=c)
    if not ok or "resolved" not in c:
        raise Refused("your recorded game does not replay (%s: %s), so its turns cannot be kept" % (kind, detail))
    if source == "search.json":
        prefix, from_turn, extra = [], 1, []     # the bench game: current rules, no human pins at all
    else:
        rung = next((r for r in e["ladder"] if r.get("cv_log") == source), None)
        if rung is None:
            raise Refused("%s is not one of this game's hand-offs" % source)
        prefix, from_turn = c["resolved"][:rung["picks"]], rung["turn"]
        k, d = probe(c, c["side"], prefix)
        if k != "decision" or d.get("turn") != from_turn:
            raise Refused("could not reach your board at T%d" % from_turn)
        extra = side_before(c["side"], from_turn, d.get("main_ordinal", 10 ** 6))
    extra = with_attackers(extra, search_attackers(log, from_turn))
    extra = with_jitte(extra, log, from_turn, c["mt"])
    try:
        picks, notes = walk(c, prefix, extra, log, from_turn)
    except Refused as err:
        want_t = (log.get("result") or {}).get("turn")
        mark = (getattr(err, "marks", None) or {}).get(want_t)
        got = rescue(c, extra, err.picks[:mark], log, want_t) if mark is not None and mark >= len(prefix) else None
        if got is None:
            raise
        picks, notes = got, ["the guided walk missed T%s (%s); a bounded search of that turn's plans found it"
                             % (want_t, err)]

    work = os.path.join(SCRATCH, key[:-5], source[:-5])     # one per source: --check-all runs them at once
    cand = write_trace(c, extra, picks, os.path.join(work, "trace"))
    got = json.load(open(cand))
    want = (log.get("result") or {}).get("turn")
    if not got.get("won") or got.get("win_turn") != want:
        raise Refused("the written trace ends T%s, the search's game T%s" % (got.get("win_turn"), want))
    rt = os.path.join(work, "roundtrip", "references", ref_dir)
    os.makedirs(rt, exist_ok=True)
    rt_path = os.path.join(rt, os.path.basename(cand))
    shutil.copyfile(cand, rt_path)
    roundtrip(rt_path, ref_dir)

    out = {"ok": True, "key": key, "source": source, "win_turn": want, "human_win": e["human_win"],
           "from_turn": from_turn, "notes": notes, "applied": False, "files": []}
    if apply_it:
        dest = ref_path
        # Keep the human's line, byte for byte, before it is replaced.
        keep = os.path.join(REPLACED, ref_dir, "human_s%d_gi%d.json" % (c["seed"], c["gi"]))
        if os.path.exists(keep):
            raise Refused("%s already exists; not overwriting it" % keep)
        os.makedirs(os.path.dirname(keep), exist_ok=True)
        shutil.copyfile(ref_path, keep)
        out["files"].append(keep)
        out["human_line_kept_at"] = keep
        stamp = {"source": source, "from_turn": from_turn, "human_win": e["human_win"],
                 "src": rr.src_tree(), "at": datetime.datetime.now(datetime.timezone.utc)
                 .isoformat(timespec="seconds")}
        text = open(cand).read()
        marker = ',\n  "decisions": ['
        if marker not in text:
            raise Refused("unexpected trace layout; not writing it")
        text = text.replace(marker, ',\n  "promoted_from_search": %s%s' % (json.dumps(stamp), marker), 1)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest + ".tmp", "w") as fh:
            fh.write(text)
        os.replace(dest + ".tmp", dest)
        out["files"].append(dest)
        out["applied"], out["reference"] = True, dest
        v = rr.load_verdicts()
        v["verdicts"][key] = rr.stamp(e, "promoted", note, source=source, reference=dest,
                                      promoted_win=want, human_line_kept_at=out.get("human_line_kept_at"))
        rr.save_verdicts(v)
        out["files"].append(rr.VERDICTS)
    return out


def check_all(jobs):
    """Dry-run promotion of every review game's search line from T1 AND from its first lost turn."""
    games = rr.load_index()["games"]
    work = []
    for e in games:
        work.append((e["key"], "search.json"))
        lost = (e.get("lost_turns") or [{}])[0].get("turn")
        r = next((r for r in e["ladder"] if r["turn"] == lost and r.get("cv_log")), None)
        if r and lost > 1:
            work.append((e["key"], r["cv_log"]))

    def run(item):
        p = subprocess.run([sys.executable, os.path.abspath(__file__), item[0], "--source", item[1], "--json"],
                           capture_output=True, text=True, cwd=ROOT)
        try:
            return item, json.loads(p.stdout.strip().splitlines()[-1])
        except Exception:                                        # noqa: BLE001
            return item, {"ok": False, "error": (p.stderr or p.stdout)[-300:]}
    n_ok = 0
    reasons = collections.Counter()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for (key, src), r in ex.map(run, work):
            n_ok += bool(r.get("ok"))
            if not r.get("ok"):
                reasons[(r.get("error") or "?").split("(")[0].split(":")[0][:60]] += 1
            print("%-4s %-55s %-18s %s" % ("ok" if r.get("ok") else "NO", key, src,
                                          "; ".join(r.get("notes") or []) if r.get("ok") else r.get("error")),
                  flush=True)
    print("\n%d of %d promotions would succeed" % (n_ok, len(work)))
    for k, v in reasons.most_common():
        print("  %3d  %s" % (v, k))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("key", nargs="?", help="review key, e.g. treasure_hunt/claude_s5_gi4.json")
    ap.add_argument("--source", default="search.json", help="search.json (default) or handoff_T<k>.json")
    ap.add_argument("--apply", action="store_true", help="write the reference (default: dry run)")
    ap.add_argument("--json", action="store_true", help="print one JSON result line")
    ap.add_argument("--note", default="", help="note stored with the 'promoted' verdict")
    ap.add_argument("--check-all", action="store_true", help="dry-run every game in the review index")
    ap.add_argument("--verbose", action="store_true", help="print every main-phase frame's probes (stderr)")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    global VERBOSE
    VERBOSE = args.verbose
    if args.check_all:
        return check_all(args.jobs)
    if not args.key:
        ap.error("a review key is required")
    try:
        out = promote(args.key, args.source, args.apply, args.note)
    except Refused as err:
        out = {"ok": False, "key": args.key, "source": args.source, "error": str(err)}
    if args.json:
        print(json.dumps(out))
    else:
        print(json.dumps(out, indent=1))
    return 0 if out["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
