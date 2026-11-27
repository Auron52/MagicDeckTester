#!/usr/bin/env python3
"""Control-scoring mode: the SCORING LIBRARY (docs/design/control-scoring-mode.md, sections 4.1-4.6, 5.6, 7.5;
stage-2 design in docs/design/control-mode-stage2-report.md).

Pure functions, no engine, no I/O except the CLI at the bottom. Units are ROUNDS; lower is better.

Definitions (doc section 4.1):
  * W      our own kill round in the control game (unwon -> H + 1, H the deck's control horizon, 12..15).
  * T_k    the credited round of our k-th control effect, T_1 <= ... <= T_n.
  * D_k(g) the delay of gauntlet game g's kill when ONLY effects 1..k are applied (measured jointly).
           D = inf is a lock. D_0 = 0.
  * The pair score   E(g) = min over k = 0..n of max(T_k, W - D_k(g)),  T_0 = -inf (so the k = 0 term is W).
  * The deck score   E = mean over gauntlet DECKS d of (mean over d's games of E(g)): equal weight per deck,
                     never one mean over all pairs, never computed from averaged delays (section 4.4).
  * The partial score of a line at a search node (section 4.5): P_d(W) = mean over d's games of
    min(W, min_k max(T_k, W - D_k(g))), and the bound LB(t) = mean over d of P_d(t).

Two forms of the pair score are implemented:
  * form="prefix"  (DEFAULT): exactly the section 4.1 formula above.
  * form="chain":  the survival-chained variant. Section 4.2's derivation requires us to be alive at T_k, and
    writes that as K >= T_k. But after prefix k-1 the gauntlet kills on K + D_{k-1}, not K, so the exact
    condition is K + D_{j-1} >= T_j for every j <= k. Solving the race for K gives
        E_chain(g) = min over k of max( max_{j<=k} (T_j - D_{j-1}), W - D_k ).
    E_chain <= E_prefix always (with D >= 0); the two differ only when an earlier effect's delay is what lets
    us survive to a later one. It changes the section 4.3a table (row 2: 4 -> 3). It is NOT the default; it is
    an open question for the USER (stage-2 report, Q-S1). The search bound LB(t) has the same form in both.

Horizon and censoring (USER Q5): the mapping is built at MAP_HORIZON = 15. A deck's horizon H is 12 by default,
at most 15. A kill round is an int, or None for "no kill within the mapping horizon".
  * W: None or W > H -> H + 1.
  * delay(K, K'): K' None (the delayed game never kills within 15)  -> inf (a lock to the horizon).
                  K None (the UNDISRUPTED game never kills within 15) -> 0 (nothing to delay, no credit).
                  otherwise K' - K (may be negative: a disrupted game can re-plan into a faster line; the
                  k = 0 term W then wins the min, so a negative delay never helps the score).
    censored="bound" instead takes the censored K' as MAP_HORIZON + 1 (a lower bound on the delay): a
    pessimistic score, reported beside the default if wanted.

Race-win share (section 5.6): a pair is a race win iff our side wins the race, which by the section 4.2
derivation is K >= E (we are on the play) or K > E (on the draw, section 4.6), with E <= H (a pair scored as
unwon is never a win). K is the UNDISRUPTED gauntlet kill (None = never within 15 = +inf).

    python3 scripts/control_score.py table43a           # the section 4.3a / 4.4 worked examples
    python3 scripts/control_score.py score <results.json> [--horizon H] [--form prefix|chain]

<results.json> (the stage-2 replay result shape, docs/design/control-mode-stage2-report.md section 5):
    {"horizon": 12,
     "games": [{"game": 0, "on_the_play": true, "w": 9,
                "effects": [{"round": 3, "kind": "removal", "card": "Parallax Wave"}, ...],
                "gauntlet": {"<deck>": [{"seed": 8100001, "k": 5, "prefix_k": [11, null]}, ...], ...},
                "without": {"<kind>": {"<deck>": [{"seed": .., "k": .., "prefix_k": [..]}, ...]}}}]}
  "prefix_k"[i] is the gauntlet game's kill round with effects 1..i+1 applied (None = no kill within 15).
  "without" (optional) holds the measured replays of the schedule with every effect of one kind removed.
"""
import json
import math
import sys

INF = math.inf
MAP_HORIZON = 15          # the mapping / baseline horizon (USER Q5: build at the largest any deck may use)
DEFAULT_HORIZON = 12      # a deck's control horizon unless its modes marker sets control_max_turns
MIN_HORIZON, MAX_HORIZON = 12, 15
FORMS = ("prefix", "chain")


# ----------------------------------------------------------------------------------------------------------
# Horizon / censoring
# ----------------------------------------------------------------------------------------------------------

def check_horizon(h):
    if not isinstance(h, int) or not MIN_HORIZON <= h <= MAX_HORIZON:
        raise ValueError("control horizon must be an int in [%d, %d], got %r" % (MIN_HORIZON, MAX_HORIZON, h))
    return h


def unwon_score(h):
    """The score of a game not won within horizon h (goldfish convention: max_turns + 1)."""
    return check_horizon(h) + 1


def censor_w(w, h):
    """Our kill round at horizon h: None (unwon) or a kill after h -> h + 1."""
    check_horizon(h)
    if w is None or w > h:
        return h + 1
    if w < 1:
        raise ValueError("a kill round is >= 1, got %r" % (w,))
    return w


def delay(base_k, ctrl_k, censored="lock", map_horizon=MAP_HORIZON):
    """D for one gauntlet game: base_k = its undisrupted kill round, ctrl_k = with the prefix applied.
    None means no kill within the mapping horizon. See the module docstring for the rules."""
    if base_k is None:
        return 0.0
    if ctrl_k is None:
        if censored == "lock":
            return INF
        if censored == "bound":
            return float(map_horizon + 1 - base_k)
        raise ValueError("censored must be 'lock' or 'bound', got %r" % (censored,))
    return float(ctrl_k - base_k)


def unwon_share(ws, h=DEFAULT_HORIZON):
    """Share of our games not won by round h (Q5: the measurement that says whether a deck needs 15).
    ws are kill rounds from games run at the mapping horizon (None = unwon by 15)."""
    ws = list(ws)
    if not ws:
        return 0.0
    return sum(1 for w in ws if w is None or w > h) / len(ws)


# ----------------------------------------------------------------------------------------------------------
# The pair score
# ----------------------------------------------------------------------------------------------------------

def _check_schedule(rounds, delays):
    if len(rounds) != len(delays):
        raise ValueError("one delay per effect prefix: %d rounds, %d delays" % (len(rounds), len(delays)))
    for a, b in zip(rounds, rounds[1:]):
        if b < a:
            raise ValueError("effect rounds must be non-decreasing: %r" % (list(rounds),))


def pair_terms(w, rounds, delays, form="prefix"):
    """The terms whose min is E(g): [W, term_1, ..., term_n]."""
    _check_schedule(rounds, delays)
    terms = [float(w)]
    if form == "prefix":
        for t, d in zip(rounds, delays):
            terms.append(max(float(t), w - d))
    elif form == "chain":
        alive = -INF                      # max_{j<=k} (T_j - D_{j-1}): the K needed to survive to T_k
        prev = 0.0                        # D_{k-1}
        for t, d in zip(rounds, delays):
            alive = max(alive, t - prev)
            terms.append(max(alive, w - d))
            prev = d
    else:
        raise ValueError("form must be one of %s, got %r" % (FORMS, form))
    return terms


def pair_score(w, rounds, delays, form="prefix"):
    """E(g) for one (control game, gauntlet game) pair. w must already be censored (censor_w)."""
    return min(pair_terms(w, rounds, delays, form))


# ----------------------------------------------------------------------------------------------------------
# Averaging: per gauntlet deck, then equal weight over decks
# ----------------------------------------------------------------------------------------------------------

def mean(xs):
    xs = list(xs)
    if not xs:
        raise ValueError("mean of nothing")
    return sum(xs) / len(xs)


def two_level_mean(by_deck):
    """{deck: [per-pair value]} -> (equal-weight mean over decks, {deck: its mean}). Section 4.1."""
    if not by_deck:
        raise ValueError("no gauntlet decks")
    per = {d: mean(v) for d, v in by_deck.items()}
    return mean(per.values()), per


def deck_partial(w, rounds, delays_per_game, form="prefix"):
    """P_d(W): mean over one gauntlet deck's games of the pair score at our kill round w (section 4.5).
    delays_per_game: one list of prefix delays per game of that deck."""
    return mean(pair_score(w, rounds, ds, form) for ds in delays_per_game)


def line_score(w, rounds, delays_by_deck, form="prefix"):
    """E of a line that kills on round w: equal-weight mean over decks of P_d(w).
    delays_by_deck: {deck: [[D_1..D_m] per game]}."""
    return mean(deck_partial(w, rounds, g, form) for g in delays_by_deck.values())


def lower_bound(t, rounds, delays_by_deck, form="prefix"):
    """LB(line, t): no continuation of a line now at round t (effects so far `rounds`, all <= t; any later
    effects on rounds >= t with ANY delays; a kill round W >= t) scores below this. Section 4.5.

    In BOTH forms LB = mean_d P_d(t), the score the line would get if it won right now:
      * the k = 0 term W >= t, and an existing term only grows with W (W - D_k is increasing in W);
      * prefix form: a future term is max(T_k, ...) >= T_k >= t >= P_d(t);
      * chain form: a future term's survival part max_{j<=k}(T_j - D_{j-1}) can sit BELOW t (T_{m+1} - D_m with
        a large D_m), but it is >= max(alive_m, T_{m+1} - D_m) >= max(alive_m, t - D_m), which is the existing
        last term evaluated at W = t (or t itself when there are no effects yet). So it is >= P_d(t) too.
    The bound holds for any mapping values, including a longer prefix that delays less."""
    if any(r > t for r in rounds):
        raise ValueError("a line at round %r cannot hold an effect on a later round: %r" % (t, list(rounds)))
    return line_score(t, rounds, delays_by_deck, form)


# ----------------------------------------------------------------------------------------------------------
# Race-win share
# ----------------------------------------------------------------------------------------------------------

def race_win(e, base_k, on_the_play, h):
    """Does the pair's control game win the race? (K >= E on the play, K > E on the draw; E <= h.)"""
    if e > h:
        return False
    k = INF if base_k is None else base_k
    return k >= e if on_the_play else k > e


# ----------------------------------------------------------------------------------------------------------
# Scoring one control game / a whole report
# ----------------------------------------------------------------------------------------------------------

def _delays_of(rec, n, censored):
    pk = list(rec.get("prefix_k") or [])
    if len(pk) != n:
        raise ValueError("gauntlet record seed=%r has %d prefix kills for %d effects"
                         % (rec.get("seed"), len(pk), n))
    return [delay(rec.get("k"), x, censored) for x in pk]


def score_game(game, h=DEFAULT_HORIZON, form="prefix", censored="lock"):
    """One control game -> {"w", "e", "per_deck": {deck: E_d}, "race": {deck: share}, "race_none": {...},
    "race_without": {kind: {deck: share}}, "e_without": {kind: E}}."""
    w = censor_w(game.get("w"), h)
    effects = list(game.get("effects") or [])
    rounds = [int(e["round"]) for e in effects]
    play = bool(game.get("on_the_play", True))
    pairs, races, races0 = {}, {}, {}
    for deck, recs in (game.get("gauntlet") or {}).items():
        es, rs, r0 = [], [], []
        for rec in recs:
            e = pair_score(w, rounds, _delays_of(rec, len(rounds), censored), form)
            es.append(e)
            rs.append(1.0 if race_win(e, rec.get("k"), play, h) else 0.0)
            r0.append(1.0 if race_win(w, rec.get("k"), play, h) else 0.0)
        pairs[deck], races[deck], races0[deck] = es, rs, r0
    e_all, per_deck = two_level_mean(pairs)
    out = {"w": w, "e": e_all, "per_deck": per_deck,
           "race": {d: mean(v) for d, v in races.items()},
           "race_none": {d: mean(v) for d, v in races0.items()},
           "race_without": {}, "e_without": {}}
    for kind, by_deck in (game.get("without") or {}).items():
        keep = [r for r, e in zip(rounds, effects) if e.get("kind") != kind]
        pw, rw = {}, {}
        for deck, recs in by_deck.items():
            es, rs = [], []
            for rec in recs:
                e = pair_score(w, keep, _delays_of(rec, len(keep), censored), form)
                es.append(e)
                rs.append(1.0 if race_win(e, rec.get("k"), play, h) else 0.0)
            pw[deck], rw[deck] = es, mean(rs)
        out["e_without"][kind] = two_level_mean(pw)[0]
        out["race_without"][kind] = rw
    return out


def score_report(results, h=None, form="prefix", censored="lock"):
    """A results document (module docstring) -> the report numbers. Per control game, E is computed with the
    two-level mean; the deck's E is the mean over its control games (each control game faces the same
    gauntlet decks, so every game weighs the same)."""
    h = check_horizon(h if h is not None else results.get("horizon", DEFAULT_HORIZON))
    games = [score_game(g, h, form, censored) for g in results.get("games", [])]
    if not games:
        raise ValueError("no control games in the results")
    decks = sorted({d for g in games for d in g["per_deck"]})
    kinds = sorted({k for g in games for k in g["e_without"]})
    raw_ws = [g.get("w") for g in results["games"]]
    return {
        "horizon": h, "form": form, "games": len(games),
        "E": mean(g["e"] for g in games),
        "W": mean(g["w"] for g in games),
        "unwon_share_12": unwon_share(raw_ws, 12),
        "unwon_share_h": unwon_share(raw_ws, h),
        "per_deck": {d: mean(g["per_deck"][d] for g in games if d in g["per_deck"]) for d in decks},
        "race": {d: mean(g["race"][d] for g in games if d in g["race"]) for d in decks},
        "race_none": {d: mean(g["race_none"][d] for g in games if d in g["race_none"]) for d in decks},
        "e_without": {k: mean(g["e_without"][k] for g in games if k in g["e_without"]) for k in kinds},
        "race_without": {k: {d: mean(g["race_without"][k][d] for g in games
                                     if k in g["race_without"] and d in g["race_without"][k])
                             for d in decks if any(k in g["race_without"] and d in g["race_without"][k]
                                                   for g in games)}
                         for k in kinds},
    }


def format_report(rep):
    lines = ["control score (form=%s, horizon %d, unwon = %d), %d control games"
             % (rep["form"], rep["horizon"], rep["horizon"] + 1, rep["games"]),
             "  E %.3f   beside raw W %.3f   (E - W %+.3f)" % (rep["E"], rep["W"], rep["E"] - rep["W"]),
             "  unwon share: %.1f%% at 12, %.1f%% at H=%d"
             % (100 * rep["unwon_share_12"], 100 * rep["unwon_share_h"], rep["horizon"]),
             "",
             "  %-28s %7s %9s %9s" % ("gauntlet deck", "E_d", "race win", "no ctrl")]
    for d in sorted(rep["per_deck"]):
        lines.append("  %-28s %7.3f %8.1f%% %8.1f%%"
                     % (d, rep["per_deck"][d], 100 * rep["race"][d], 100 * rep["race_none"][d]))
    if rep["e_without"]:
        lines += ["", "  without one effect kind (measured replays of the filtered schedule):"]
        for k in sorted(rep["e_without"]):
            rw = rep["race_without"].get(k) or {}
            share = mean(rw.values()) if rw else float("nan")
            lines.append("    -%-14s E %.3f (%+.3f)   race win %.1f%% (all-deck mean)"
                         % (k, rep["e_without"][k], rep["e_without"][k] - rep["E"], 100 * share))
    return "\n".join(lines)


# ----------------------------------------------------------------------------------------------------------
# The worked examples of the design doc (also the unit test's fixtures)
# ----------------------------------------------------------------------------------------------------------

# Section 4.3a: W = 9, Parallax Wave on round 3, Opalescence on round 4. (label, D_1, D_2, expected E).
TABLE_43A = [
    ("creature deck, Wave takes its whole early board (kill 5 -> 11)", 6.0, INF, 3.0),
    ("creature deck that rebuilds quickly (kill 5 -> 7)", 2.0, INF, 4.0),
    ("creatures plus a burn finish", 1.0, 3.0, 6.0),
    ("burn, or a spell-combo kill", 0.0, 0.0, 9.0),
]
TABLE_43A_W, TABLE_43A_ROUNDS = 9, [3, 4]
MIX_CREATURE = [0.5, 0.5, 0.0, 0.0]          # -> 3.5
MIX_MIXED = [0.40, 0.30, 0.15, 0.15]         # -> 4.65


def table43a(form="prefix"):
    rows = [(lab, pair_score(TABLE_43A_W, TABLE_43A_ROUNDS, [d1, d2], form)) for lab, d1, d2, _ in TABLE_43A]
    mix = lambda w: sum(p * e for p, (_, e) in zip(w, rows))
    return rows, mix(MIX_CREATURE), mix(MIX_MIXED)


def table44():
    """Section 4.4: W = 10, effect on round 4, a full lock vs deck A, nothing vs deck B -> 7.0 per pair."""
    by_deck = {"A": [pair_score(10, [4], [INF])], "B": [pair_score(10, [4], [0.0])]}
    return two_level_mean(by_deck)[0]


def _main(argv):
    if len(argv) >= 1 and argv[0] == "table43a":
        for form in FORMS:
            rows, a, b = table43a(form)
            print("form=%s  (W = %d, effects on rounds %s)" % (form, TABLE_43A_W, TABLE_43A_ROUNDS))
            for lab, e in rows:
                print("  %-66s E = %g" % (lab, e))
            print("  creature-heavy mix 50/50: %.2f    mixed 40/30/15/15: %.2f" % (a, b))
        print("section 4.4 per pair then average: %.1f" % table44())
        return 0
    if len(argv) >= 2 and argv[0] == "score":
        opts = argv[2:]
        h = int(opts[opts.index("--horizon") + 1]) if "--horizon" in opts else None
        form = opts[opts.index("--form") + 1] if "--form" in opts else "prefix"
        censored = opts[opts.index("--censored") + 1] if "--censored" in opts else "lock"
        with open(argv[1]) as f:
            rep = score_report(json.load(f), h, form, censored)
        print(format_report(rep))
        if "--json" in opts:
            print(json.dumps(rep, indent=1, sort_keys=True))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
