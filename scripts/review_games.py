#!/usr/bin/env python3
"""Review played games for MISTAKES, INEFFICIENCIES and MISSING HEURISTICS.

Reads the game-log JSON that `mtg --log-dir <dir>` writes (one file per game) and turns it into
something a reviewer can act on -- where "a reviewer" is a human OR an AI agent.

ONE FORMAT FOR BOTH AUDIENCES. The default text output is the canonical one and it is designed
to be parsed as well as read: every line starts with a stable UPPER_SNAKE tag, fields are
`key=value`, and no field order ever changes. So an agent can grep/split it directly and a human
can read the same bytes. `--json` exists for pipelines that want typed values without splitting
strings; it carries exactly the same facts, nothing more.

Why this exists. The engine's cost investigations kept producing aggregates -- which cards are
slow, which turns branch most -- and an aggregate cannot tell you whether the work was
JUSTIFIED. Only a real game can, and only if you can see it. See
docs/design/fungus-slow-rollout-diagnosis-2026-09-30.md for the investigation this serves.

USAGE
    review_games.py rank     <dir>                        triage: order games by degeneracy
    review_games.py show     <game.json|dir> [...]        turn-by-turn
    review_games.py findings <game.json|dir> [--json]     detectors
    review_games.py summary  <dir> [--json]               detectors rolled up over a run
    ... plus --min-severity low|medium|high

HOW TO READ (and parse) A FINDING LINE
    FINDING id=UNSPENT_MANA sev=medium conf=heuristic game=0 turn=4 phase=MAIN_1 \\
            msg="2 land(s) untapped after main" untapped=Forest,Secluded_Courtyard cheapest_cmc=1

    conf=certain    arithmetic from the log alone; no judgement, safe to act on.
    conf=heuristic  needs board judgement the log does NOT carry -- mana colour, depletion
                    counters, summoning sickness, instant-speed holds. MAY be a false positive.
                    Never report one as a confirmed bug without checking the board yourself.

    That distinction is the whole point of the confidence field: the log records what happened,
    not what was legal, so "wasteful" lines are often correct for a reason it cannot show.
"""
import json, sys, glob, os, collections, argparse

SEV_ORDER = {"low": 0, "medium": 1, "high": 2}

# Land names are recognised by substring because a token/permanent entry carries no type bits
# beyond `isLand`, and cards still in HAND have no board entry at all to read it from.
_LAND_HINTS = ("Forest", "Marsh", "Courtyard", "Bog", "Woodlot", "Island", "Swamp",
               "Mountain", "Plains", "Wastes", "Tower", "Temple")


# ---------------------------------------------------------------- card data

def load_cards(cards_json="src/cards/data/cards.json"):
    """-> (cmc_by_name, is_creature_by_name, creature_only_land_names).

    `creature_only_land_names` is the set of lands whose COLOURED mana may be spent only on a
    creature spell (`colored_creature_only`, e.g. Secluded Courtyard, which otherwise adds {C}).
    Without it the tool flags a correct refusal as an inefficiency -- verified on Fungus game 50,
    where the only untapped land was a Secluded Courtyard and the cheapest card in hand was
    Wild Growth, an ENCHANTMENT the Courtyard's coloured mana cannot legally pay for.
    """
    try:
        raw = json.load(open(cards_json))
    except Exception:
        return {}, {}, set()
    cmc, iscr, conly = {}, {}, set()
    for c in raw.get("cards", []):
        name = c.get("name")
        iscr[name] = "Creature" in (c.get("types") or [])
        if (c.get("parameters") or {}).get("colored_creature_only"):
            conly.add(name)
        mc = c.get("mana_cost")
        if not isinstance(mc, str):
            continue
        total, num = 0, ""
        for ch in mc:
            if ch.isdigit():
                num += ch
            else:
                if num:
                    total += int(num); num = ""
                if ch.isalpha():
                    total += 1            # one coloured pip
        if num:
            total += int(num)
        cmc[name] = total
    return cmc, iscr, conly


# ---------------------------------------------------------------- log helpers

def is_token(p):
    """Tokens are named by characteristics ("1/1 Saproling Token") and have no card entry."""
    return p["cardName"].endswith("Token")


def is_land_name(name):
    return any(h in name for h in _LAND_HINTS)


def num_to_name(log):
    m = {}
    for name, nums in log["cardNumbering"].items():
        for n in nums:
            m[n] = name
    return m


def board_parts(bf):
    """-> (untapped_lands, tapped_lands, non_land_perms, Counter(token -> count))"""
    lu, lt, perms, tok = [], [], [], collections.Counter()
    for p in bf:
        if is_token(p):
            tok[p["cardName"]] += 1
            continue
        ctrs = "".join(f"+{c['count']}{c['kind'][:4]}" for c in p.get("counters", []))
        if p.get("isLand"):
            dep = "".join(f"-dep{c['count']}" for c in p.get("counters", [])
                          if c["kind"] == "depletion")
            (lt if p.get("tapped") else lu).append(p["cardName"] + dep)
        else:
            perms.append(p["cardName"] + ctrs)
    return lu, lt, perms, tok


def tok_count(bf):
    return sum(1 for p in bf if is_token(p))


def q(s):
    """Quote a value so a parser can split on whitespace safely."""
    s = str(s)
    return s.replace(" ", "_").replace('"', "'")


# ---------------------------------------------------------------- detectors

def detect(log, cards):
    costs, iscr, conly = cards
    out = []
    g = log["gameNumber"]
    n2n = num_to_name(log)
    win_turn = log["result"]["turn"]

    def add(did, sev, conf, turn, phase, msg, **ev):
        out.append({"id": did, "severity": sev, "confidence": conf, "game": g,
                    "turn": turn, "phase": phase, "message": msg, "evidence": ev})

    for t in log["turns"]:
        ph, turn, b = t["phase"], t["turn"], t["boardAfter"]
        lu, lt, perms, tok = board_parts(b["battlefield"])
        hand = [n2n.get(c, f"#{c}") for c in b.get("hand", [])]
        acts = t["actions"]
        cast = [a for a in acts if a.get("type") == "CAST_SPELL"]
        lands_played = [a for a in acts if a.get("type") == "PLAY_LAND"]
        # Strip the land's own decoration ("-dep2") before matching against card names.
        lu_base = [l.split("-dep")[0] for l in lu]
        # If EVERY untapped land is creature-only (Secluded Courtyard), only creature spells are
        # payable with its coloured mana -- its other mode adds {C}, which pays no coloured pip.
        creature_only_mana = bool(lu_base) and all(l in conly for l in lu_base)
        spells_in_hand = [(h, costs[h]) for h in hand
                          if h in costs and not is_land_name(h)
                          and not (creature_only_mana and not iscr.get(h, False))]

        # A main phase on the WINNING turn is exempt from the "you could have cast something"
        # detectors: a creature cast now has summoning sickness and cannot attack, and the
        # attack that follows is already lethal, so casting nothing costs nothing. Verified
        # false positive (Fungus game 24 T6: 5 lands untapped, Tukatongue Thallid in hand,
        # turn-6 attack already lethal) -- without this the tool cries wolf on every win.
        if ph == "MAIN_1" and turn < win_turn:
            land_in_hand = [h for h in hand if is_land_name(h)]
            if land_in_hand and not lands_played:
                add("LAND_HELD", "medium", "heuristic", turn, ph,
                    f"no land played with {len(land_in_hand)} land(s) in hand",
                    lands_in_hand=",".join(land_in_hand),
                    board_lands=len(lu) + len(lt))

            if lu and spells_in_hand:
                mn, mnc = min(spells_in_hand, key=lambda x: x[1])
                if mnc <= len(lu):
                    did = "IDLE_MAIN" if not cast else "UNSPENT_MANA"
                    sev = "high" if not cast else "medium"
                    add(did, sev, "heuristic", turn, ph,
                        (f"cast nothing with {len(lu)} untapped land(s)" if not cast
                         else f"{len(lu)} land(s) untapped after main"),
                        untapped=",".join(lu), cheapest=mn, cheapest_cmc=mnc,
                        hand=",".join(hand))

        if ph == "COMBAT":
            for a in acts:
                if a.get("type") != "ATTACK":
                    continue
                dmg, after = a.get("damage", 0), a.get("oppLife", 0)
                before = after + dmg
                if after < 0 and before > 0 and dmg >= 2 * before:
                    add("OVERKILL", "low", "certain", turn, ph,
                        f"attacked for {dmg} into {before} life",
                        damage=dmg, life_before=before, wasted=dmg - before,
                        bodies=sum(tok.values()) + len(perms))

    # ---- whole-game -------------------------------------------------------------------
    peak = peak_turn = peak_tok = 0
    for t in log["turns"]:
        bf = t["boardAfter"]["battlefield"]
        if len(bf) > peak:
            peak, peak_turn, peak_tok = len(bf), t["turn"], tok_count(bf)
    if peak:
        add("BOARD_PEAK", "low", "certain", peak_turn, "-",
            f"board peaked at {peak} permanents ({peak_tok} tokens)",
            peak_board=peak, peak_tokens=peak_tok)

    # RESOURCE_OVERPRODUCTION -- the "is this degeneracy justified?" detector, and the reason
    # this tool exists. If the lethal attack needed far fewer bodies than the board held, the
    # search spent its time enumerating a resource the win never used.
    final = None
    for t in log["turns"]:
        for a in t["actions"]:
            if a.get("type") == "ATTACK" and a.get("oppLife", 1) <= 0:
                final = (t, a)
    if final:
        t, a = final
        bf = t["boardAfter"]["battlefield"]
        ntok, dmg = tok_count(bf), a.get("damage", 0)
        before = dmg + a.get("oppLife", 0)
        if ntok and before > 0 and ntok > 2 * before:
            add("RESOURCE_OVERPRODUCTION", "high", "heuristic", t["turn"], t["phase"],
                f"{ntok} tokens on board to deal {before} lethal",
                tokens=ntok, lethal_needed=before, damage_dealt=dmg,
                surplus_lower_bound=ntok - before,
                note="lower_bound_ignores_lords_and_anthems")

    last = log["turns"][-1]["boardAfter"]
    still = {n2n.get(c, f"#{c}") for c in last.get("hand", [])}
    stuck = sorted({c["cardName"] for c in log["openingHand"]} & still)
    if stuck:
        add("CARD_NEVER_CAST", "low", "heuristic", log["turns"][-1]["turn"], "-",
            f"opening-hand card(s) never cast: {', '.join(stuck)}",
            cards=",".join(stuck), win_turn=win_turn)
    return out


def fmt_finding(x):
    ev = " ".join(f"{k}={q(v)}" for k, v in x["evidence"].items())
    return (f"FINDING id={x['id']} sev={x['severity']} conf={x['confidence']} "
            f"game={x['game']} turn={x['turn']} phase={x['phase']} "
            f"msg=\"{x['message']}\" {ev}".rstrip())


# ---------------------------------------------------------------- show

def show(path):
    log = json.load(open(path))
    n2n = num_to_name(log)
    mulls = len(log.get("mulliganSequence", [])) - 1
    print(f"GAME game={log['gameNumber']} seed={log['seed']} deck={log['deckId']} "
          f"win_turn={log['result']['turn']} mulligans={mulls}")
    print(f"OPENING {' '.join(q(c['cardName']) for c in log['openingHand'])}")
    prev = 0
    for t in log["turns"]:
        b = t["boardAfter"]
        bf = b["battlefield"]
        lu, lt, perms, tok = board_parts(bf)
        ntok = sum(tok.values())
        plays = []
        for a in t["actions"]:
            ty, nm = a.get("type"), a.get("cardName", "")
            if ty == "DRAW":        plays.append(f"draw:{q(nm)}")
            elif ty == "PLAY_LAND": plays.append(f"land:{q(nm)}")
            elif ty == "CAST_SPELL":
                x = a.get("chosenX")
                plays.append(f"cast:{q(nm)}" + (f":X={x}" if x not in (None, 0) else ""))
            elif ty == "ATTACK":
                plays.append(f"attack:dmg={a.get('damage')}:opp={a.get('oppLife')}")
        if not plays and ntok == prev:
            continue
        print(f"TURN turn={t['turn']} phase={t['phase']} board={len(bf)} "
              f"lands={len(lu) + len(lt)} untapped={len(lu)} perms={len(perms)} "
              f"tokens={ntok} tokens_delta={ntok - prev:+d} opp_life={b['opponentLife']}")
        prev = ntok
        if plays:
            print(f"  PLAYS {' '.join(plays)}")
        if perms:
            print(f"  PERMS {' '.join(q(p) for p in perms)}")
        hand = [n2n.get(c, f"#{c}") for c in b.get("hand", [])]
        if hand:
            print(f"  HAND {' '.join(q(h) for h in hand)}")
        if lu:
            print(f"  UNTAPPED {' '.join(q(l) for l in lu)}")


# ---------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(add_help=True, description=__doc__.split("\n")[0])
    ap.add_argument("mode", choices=["rank", "show", "findings", "summary"])
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--json", action="store_true",
                    help="typed output for pipelines; same facts as the text form")
    ap.add_argument("--min-severity", default="low", choices=["low", "medium", "high"])
    a = ap.parse_args()

    files = []
    for p in a.paths:
        files.extend(sorted(glob.glob(os.path.join(p, "*.json"))) if os.path.isdir(p) else [p])
    if not files:
        print("no game logs found", file=sys.stderr)
        return 2

    if a.mode == "rank":
        rows = []
        for f in files:
            g = json.load(open(f))
            mx = mt = pk = 0
            for t in g["turns"]:
                bf = t["boardAfter"]["battlefield"]
                if len(bf) > mx:
                    mx, pk = len(bf), t["turn"]
                mt = max(mt, tok_count(bf))
            rows.append({"game": g["gameNumber"], "win_turn": g["result"]["turn"],
                         "peak_board": mx, "peak_tokens": mt, "peak_turn": pk,
                         "file": os.path.basename(f)})
        rows.sort(key=lambda r: -r["peak_board"])
        if a.json:
            print(json.dumps(rows, indent=1))
        else:
            for r in rows:
                print(f"RANK game={r['game']} win_turn={r['win_turn']} "
                      f"peak_board={r['peak_board']} peak_tokens={r['peak_tokens']} "
                      f"peak_turn={r['peak_turn']} file={r['file']}")
        return 0

    if a.mode == "show":
        for f in files:
            show(f)
        return 0

    cards = load_cards()
    allf = []
    for f in files:
        log = json.load(open(f))
        for fi in detect(log, cards):
            if SEV_ORDER[fi["severity"]] >= SEV_ORDER[a.min_severity]:
                fi["file"] = os.path.basename(f)
                allf.append(fi)

    if a.mode == "summary":
        cnt = collections.Counter((x["id"], x["severity"], x["confidence"]) for x in allf)
        if a.json:
            print(json.dumps({"games": len(files), "findings": len(allf),
                              "by_detector": [{"id": k[0], "severity": k[1],
                                               "confidence": k[2], "count": v}
                                              for k, v in cnt.most_common()]}, indent=1))
        else:
            print(f"SUMMARY games={len(files)} findings={len(allf)}")
            for (did, sev, conf), v in cnt.most_common():
                print(f"DETECTOR id={did} sev={sev} conf={conf} count={v}")
        return 0

    if a.json:
        print(json.dumps(allf, indent=1))
    else:
        for x in sorted(allf, key=lambda y: (-SEV_ORDER[y["severity"]], y["game"], y["turn"])):
            print(fmt_finding(x))
    return 0


if __name__ == "__main__":
    sys.exit(main())
