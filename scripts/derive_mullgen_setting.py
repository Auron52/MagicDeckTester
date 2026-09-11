#!/usr/bin/env python3
"""Derive a deck's mulligan-GENERATION setting (value_play.mull_gen_*) by measuring it.

Runs at the END of a value-leaf run, where the deck's play settings and value_trust_depth are known.
Answers "which (depth, budget) should this deck label its keep table at?" by scoring the SAME random
openers under each candidate and comparing them to the deck's own shipped play policy.

WHY MEASURE INSTEAD OF DEFAULTING (user, 2026-08-15): "We don't always want to take d3 b3 just
because it is the default. We want to figure out the best option for the deck." The direction is
genuinely deck-shaped -- on fivecolour, LOWER depth is 2.4x MORE expensive
(mullgen-depth-cost-vs-quality.md) -- so a default is a guess, and a wrong guess costs hours-to-days
of generation.

THE TWO RULES, both measured (mullgen-setting-is-a-trust-question.md):

  * TRUSTED at the shipped play depth -> emit NO override, i.e. generate at play settings. Not merely
    acceptable: for such a deck the play settings are frequently the CHEAPEST arm available (slivers
    1,496 units/rollout vs 2,431 for d3 b3) AND perfect by construction, because reaching the value
    leaf terminates the line instead of playing the game out. There is no trade-off to arbitrate.
  * OTHERWISE -> pick the CHEAPEST (depth, budget) pair that clears a rank-fidelity floor. Speed is
    the binding constraint for generation ("within reason"), because a profile that is too expensive
    to generate does not exist at all -- but a labeller that reorders hands produces a different
    policy, so the floor is real and not decorative.

WHY (depth, budget) PAIRS AND NOT A DEPTH LADDER. Reaching the value leaf needs depth >= trust AND
enough budget to COMPLETE that depth; the escalation ladder commits the deepest COMPLETED pass, so a
starved budget pays for an abandoned deeper pass and commits the shallower line anyway. Cost is
therefore NOT monotonic in depth: on slivers d3 b20 costs 11,551 units while d5 b20 costs 1,496 --
going deeper at the same budget is 7.7x cheaper.

WHY RANDOM OPENERS AND NOT BUCKET COMPS. The comp scorer needs a committed exhaustive sidecar for its
bucket map, but that sidecar is an OUTPUT of the generation this script configures -- on a new deck it
does not exist exactly when the derivation wants it. MTG_SCORE_HANDS needs no buckets, no discovery
and no prior profile.
"""
import argparse
import json
import math
import os
import pathlib
import re
import subprocess
import sys

BIN_CANDIDATES = ["build/Release/mtg-analyze", "build/Profile/mtg-analyze"]


def spearman(a, b):
    n = len(a)
    if n < 3:
        return float("nan")

    def ranks(v):
        order = sorted(range(n), key=lambda i: v[i])
        r = [0.0] * n
        i = 0
        while i < n:
            j = i
            while j + 1 < n and v[order[j + 1]] == v[order[i]]:
                j += 1
            avg = (i + j) / 2.0 + 1.0
            for k in range(i, j + 1):
                r[order[k]] = avg
            i = j + 1
        return r

    ra, rb = ranks(a), ranks(b)
    ma, mb = sum(ra) / n, sum(rb) / n
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    da = math.sqrt(sum((x - ma) ** 2 for x in ra))
    db = math.sqrt(sum((y - mb) ** 2 for y in rb))
    return num / (da * db) if da > 0 and db > 0 else float("nan")


def builtin_default_play(hdr="src/ai/MulliganProfile.h"):
    """(depth, budget_ms) of MulliganProfile::BuiltinDefaultPlay(), READ FROM THE HEADER.

    Not hard-coded: this is the reference a no-value_play deck is scored against, so a Python copy
    that silently drifted from the C++ would make every such derivation measure a policy no deck
    plays -- the same class of bug as the value-sidecar-less review mode (8811bb28). Parsed, and
    fails loudly rather than guessing.
    """
    try:
        src = open(hdr).read()
    except OSError as e:
        raise SystemExit("cannot read %s to resolve the built-in default play settings: %s" % (hdr, e))
    body = re.search(r"BuiltinDefaultPlay\s*\(\s*\)\s*\{(.*?)\}", src, re.S)
    if not body:
        raise SystemExit("BuiltinDefaultPlay() not found in %s -- resolve the play reference by hand" % hdr)
    d = re.search(r"target_depth\s*=\s*(\d+)", body.group(1))
    b = re.search(r"budget_ms\s*=\s*(\d+)", body.group(1))
    if not d or not b:
        raise SystemExit("BuiltinDefaultPlay() in %s no longer sets target_depth/budget_ms literally" % hdr)
    return int(d.group(1)), int(b.group(1))


def write_mull_gen(vpath, depth, budget):
    """Set value_play.mull_gen_{depth,budget_ms} with a TEXT edit, preserving the file's bytes.

    A `json.dump(..., indent=1)` re-serialises the WHOLE sidecar, which reformats every file that
    was not already stored at indent=1 -- these are 50-60 KB models, so writing 9 decks produced a
    110,991-line diff for 18 keys (2026-09-08), and it also destroys hand formatting a dump cannot
    reproduce (Fluctuator carries a blank line before its final brace). So edit in place: replace
    the two values if present, else insert them before the value_play object's closing brace,
    matching the indentation of the keys already there (or staying on one line if minified).
    """
    txt = vpath.read_text()
    i = txt.find('"value_play"')
    if i < 0:
        raise SystemExit("no value_play object in %s" % vpath)
    open_br = txt.index("{", i)
    depth_ct, end = 0, None
    for j in range(open_br, len(txt)):            # brace-match to find this object's end
        if txt[j] == "{":
            depth_ct += 1
        elif txt[j] == "}":
            depth_ct -= 1
            if depth_ct == 0:
                end = j
                break
    if end is None:
        raise SystemExit("unbalanced braces in %s" % vpath)
    span = txt[open_br:end + 1]

    if '"mull_gen_depth"' in span:                 # update in place
        new = re.sub(r'("mull_gen_depth"\s*:\s*)\d+', r"\g<1>%d" % depth, span)
        new = re.sub(r'("mull_gen_budget_ms"\s*:\s*)\d+', r"\g<1>%d" % budget, new)
    else:                                          # insert before the closing brace
        body = span[1:-1]
        if "\n" in body:
            m = re.search(r'\n([ \t]*)"', body)     # indentation of the first key
            ind = m.group(1) if m else "  "
            m2 = re.search(r"\n([ \t]*)$", body)    # indentation of the closing brace
            close_ind = m2.group(1) if m2 else ""
            new = ("{" + body.rstrip() +
                   ',\n%s"mull_gen_depth": %d,\n%s"mull_gen_budget_ms": %d\n%s}'
                   % (ind, depth, ind, budget, close_ind))
        else:
            new = ("{" + body.rstrip() +
                   ', "mull_gen_depth": %d, "mull_gen_budget_ms": %d}' % (depth, budget))

    out = txt[:open_br] + new + txt[end + 1:]
    d = json.loads(out)                            # must still parse, and carry what we meant
    assert d["value_play"]["mull_gen_depth"] == depth
    assert d["value_play"]["mull_gen_budget_ms"] == budget
    vpath.write_text(out)


def score(binary, deck, cards, depth, budget, hands, R, seed):
    """-> (labels, units_per_rollout). Labels are the play/draw mean per opener."""
    env = dict(os.environ,
               MTG_SCORE_COMPS="1", MTG_SCORE_HANDS=str(hands), MTG_SCORE_R=str(R),
               MTG_EQUIV_DEPTH=str(depth), MTG_SCORE_BUDGET_MS=str(budget),
               MTG_SCORE_HAND_SEED=str(seed))
    p = subprocess.run([binary, str(deck), "--cards-json", cards],
                       capture_output=True, text=True, env=env)
    if p.returncode != 0:
        raise SystemExit("scorer failed (d%s b%s):\n%s" % (depth, budget, p.stderr[-2000:]))
    labels = []
    for line in p.stdout.splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        try:
            dm = float(parts[1].split()[0])
            pm = float(parts[2].split()[0])
        except (ValueError, IndexError):
            continue
        labels.append((dm + pm) / 2.0)
    m = re.search(r"units_per_rollout=([0-9.]+)", p.stderr)
    return labels, (float(m.group(1)) if m else 0.0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck", help="path to the decklist (.txt/.cod)")
    ap.add_argument("--cards-json", default="src/cards/data/cards.json")
    ap.add_argument("--hands", type=int, default=48)
    ap.add_argument("-R", type=int, default=24)
    ap.add_argument("--seed", type=int, default=424242)
    ap.add_argument("--floor-rho", type=float, default=0.99,
                    help="minimum rank fidelity vs the deck's shipped play policy")
    ap.add_argument("--write", action="store_true",
                    help="write the pick into the deck's .value.json value_play")
    args = ap.parse_args()

    deck = pathlib.Path(args.deck)
    binary = next((b for b in BIN_CANDIDATES if os.path.exists(b)), None)
    if binary is None:
        raise SystemExit("no mtg-analyze binary; run ./build.sh first")

    vpath = deck.parent / (deck.stem + ".value.json")
    if not vpath.exists():
        raise SystemExit("no value sidecar at %s -- run the value-leaf first (this step is its "
                         "LAST phase, and it needs the play settings the matrix derived)" % vpath)
    vjson = json.load(open(vpath))
    vp = vjson.get("value_play") or {}
    trust = vjson.get("value_trust_depth")
    play_d, play_b = int(vp.get("target_depth") or 0), int(vp.get("budget_ms") or 0)
    play_src = "value_play"
    if not play_d or not play_b:
        # A deck with no value_play (or an unenabled one) is NOT a deck with no play policy -- it is a
        # deck that plays at MulliganProfile::BuiltinDefaultPlay(), which is what ResolvePlaySettings
        # falls through to. Refusing here was self-defeating: the decks with no block are exactly the
        # decks whose GENERATION silently inherits that default, i.e. the ones with the most to gain.
        # KittyEquipment lost 4.44x for weeks this way, and phase F reported success while doing it.
        play_d, play_b = builtin_default_play()
        play_src = "BuiltinDefaultPlay (deck ships no enabled value_play)"

    print("deck=%s  play=d%d b%d  [%s]  value_trust_depth=%s"
          % (deck.stem, play_d, play_b, play_src, trust))

    # ---------------------------------------------------------------------------- always MEASURE
    # There is deliberately NO trusted short-circuit. It is tempting -- on a trusted deck, reaching
    # the value leaf terminates the rollout instead of playing it out, so play settings are often the
    # CHEAPEST arm as well as exact (slivers: 1,496 units/rollout vs 2,431 for d3 b3). But "often" is
    # not "always", and the counterexample is not marginal: burn is trusted at its play depth (V5 <=
    # d6) and its play settings still cost 18,844 units/rollout, against 1,253 for d1 b3 at rho 0.999
    # -- 15x the cost for +0.001 rank fidelity. Whether the leaf truncates enough to pay for the depth
    # is a property of the DECK, so it is measured, not assumed (user: "We don't always want to take
    # d3 b3 just because it is the default. We want to figure out the best option for the deck.").
    #
    # Trust still shapes the CANDIDATE SET -- the trust depth is where the cliff can be, so it must be
    # on the list -- it just does not decide the answer.
    if trust is not None:
        print("  (trusted at V%d%s -- trust depth is a CANDIDATE, not a short-circuit; see burn)"
              % (trust, " <= play depth" if trust <= play_d else " > play depth"))
    cands = [(1, 3), (2, 3), (3, 3), (3, 20)]
    if trust is not None:
        # A BUDGET LADDER at the trust depth, not just b3 and play_b (USER, 2026-09-08: "trust at 5
        # means we seriously consider d5 b20 or equivalent"). Reaching the leaf TERMINATES the
        # rollout, but only if the budget COMPLETES the depth -- so a starved budget pays for an
        # abandoned pass AND plays the game out, and cost is NOT monotonic in budget. The old pair
        # {b3, play_b} could not express the middle, and on slivers the optimum lives exactly there:
        # d5 b10 is rho 1.0000 at 0.962x play, while the b3 it used to pick reorders hands (0.9986)
        # and d5 b40/b80 cost 1.13x/1.36x. Knights shows the same basin (d5 b6 at 0.960x).
        for b in sorted({3, 6, 10, 20, play_b}):
            cands.append((trust, b))
    cands.append((play_d, play_b))
    seen, ordered = set(), []
    for c in cands:
        if c not in seen:
            seen.add(c)
            ordered.append(c)

    print("\nscoring %d openers at R=%d against the play reference d%d b%d ...\n"
          % (args.hands, args.R, play_d, play_b))
    ref, ref_cost = score(binary, deck, args.cards_json, play_d, play_b, args.hands, args.R, args.seed)

    print("%-10s %8s %12s %10s" % ("arm", "rho", "units/roll", "cost_x"))
    rows = []
    for (d, b) in ordered:
        if (d, b) == (play_d, play_b):
            lab, cost = ref, ref_cost
            rho = 1.0
        else:
            lab, cost = score(binary, deck, args.cards_json, d, b, args.hands, args.R, args.seed)
            if len(lab) != len(ref):
                print("  d%-2d b%-3d SKIPPED (label count mismatch)" % (d, b))
                continue
            rho = spearman(lab, ref)
        rows.append(dict(d=d, b=b, rho=rho, cost=cost))
        print("d%-2d b%-4d %8.4f %12.0f %10.3f"
              % (d, b, rho, cost, cost / ref_cost if ref_cost else float("nan")))

    ok = [r for r in rows if r["rho"] >= args.floor_rho]
    if not ok:
        best = max(rows, key=lambda r: r["rho"])
        print("\nNO candidate clears the rho floor %.3f. Best is d%d b%d at %.4f."
              % (args.floor_rho, best["d"], best["b"], best["rho"]))
        print("Falling back to PLAY SETTINGS (no override) -- generating under a labeller that "
              "measurably\nreorders hands is not a saving, it is a different policy.")
        return

    # PREFER AN EXACT ARM THAT IS CHEAPER THAN PLAY. rho 1.0 means the labeller orders hands
    # IDENTICALLY to the policy the deck ships, i.e. zero policy distortion -- categorically
    # different from merely clearing the floor, because the floor's whole justification is that a
    # labeller which reorders hands produces a DIFFERENT policy. When such an arm also costs less
    # than play there is no trade to arbitrate (the trust-question doc's rule 1, made operational):
    # take it. Otherwise fall through to the cheapest arm clearing the floor, unchanged.
    # Measured effect: slivers d5 b3 (0.9986, 0.927x) -> d5 b10 (1.0000, 0.962x); Knights
    # d5 b3 -> d5 b6 (both already exact, 0.7% cheaper). Auras/burn/hinata UNCHANGED, because
    # their only exact arm IS play settings, so nothing is cheaper and the cheap arm still wins
    # (Auras 0.493x at 0.9970, burn 0.221x at 0.9991 -- exactness there would cost 2x and 4.5x).
    # ...but the preference is BOUNDED, because "exact" is not worth any price. Generation speed is
    # the binding constraint ("for profile generation speed is actually more important than quality
    # ... within reason"), so exactness is taken only when its premium over the cheapest ACCEPTABLE
    # arm is small. Measured premiums, 2026-09-08: Knights +0% and slivers +3.8% (clearly worth it
    # -- slivers' b3 reorders hands for a 3.5% saving), Goblins +15%, CritterLifegain +39%,
    # Fluctuator +152% (d1 b3 0.389x rho 0.9963 -> d4 b20 0.982x rho 1.0000 -- 2.5x the cost for
    # +0.0037 rho, which is exactly the trade this cap exists to refuse). An unbounded version of
    # this rule was written and reverted the same day for that Fluctuator case.
    EXACT = 0.99995
    PREMIUM_CAP = 1.25          # judgement constant; retune with the measured premiums above
    cheap = min(ok, key=lambda r: r["cost"])
    exact_cheaper = [r for r in rows if r["rho"] >= EXACT and r["cost"] < ref_cost]
    best_exact = min(exact_cheaper, key=lambda r: r["cost"]) if exact_cheaper else None
    if best_exact is not None and best_exact["cost"] <= cheap["cost"] * PREMIUM_CAP:
        pick = best_exact
        print("\nPICK: d%d b%d  (rho %.4f EXACT, cheaper than play, and only %+.1f%% over the "
              "cheapest acceptable arm; %.2fx the play cost)"
              % (pick["d"], pick["b"], pick["rho"],
                 100.0 * (pick["cost"] / cheap["cost"] - 1.0),
                 pick["cost"] / ref_cost if ref_cost else float("nan")))
    else:
        pick = cheap
        if best_exact is not None:
            print("\n  (exact arm d%d b%d rejected: %+.1f%% over the cheapest acceptable arm, "
                  "past the %.0f%% cap)"
                  % (best_exact["d"], best_exact["b"],
                     100.0 * (best_exact["cost"] / cheap["cost"] - 1.0), 100.0 * (PREMIUM_CAP - 1.0)))
        print("\nPICK: d%d b%d  (rho %.4f >= floor %.3f, cheapest clearing it; %.2fx the play cost)"
              % (pick["d"], pick["b"], pick["rho"], args.floor_rho,
                 pick["cost"] / ref_cost if ref_cost else float("nan")))
    if (pick["d"], pick["b"]) == (play_d, play_b):
        print("  == play settings; emit no override.")
    elif args.write:
        write_mull_gen(vpath, pick["d"], pick["b"])
        print("  written to %s" % vpath.name)
    else:
        print("  (re-run with --write to record it)")


if __name__ == "__main__":
    main()
