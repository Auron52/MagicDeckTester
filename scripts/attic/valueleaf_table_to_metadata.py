#!/usr/bin/env python3
"""Persist the value-leaf x heuristic DEPTH TABLE into each deck's model metadata (`<deck>.value.json`).

Motivation (2026-07-16, user): the by-depth "win turns by heuristic depth and value-leaf depth" table is what
tells us WHEN to trust the value leaf vs. fall back to the heuristic. It was being *measured*
(scripts/valueleaf_depth_matrix.py -> logs/eval/*.txt) but only a single collapsed scalar (`value_trust_depth`)
was ever written into the model, and the table itself lived only in a gitignored scratch log. That is the bug:
the table should live IN the metadata so the fallback decision is inspectable and reproducible from the model.

This tool ingests a DEPTH MATRIX log (produced by valueleaf_depth_matrix.py) and, per deck, writes:
  - `value_leaf_table` : the full by-depth table (hdepths+heuristic_lp, vdepths+value_leaf_lp) + provenance
                         (games/seeds/value_min_depth) and the derivation parameters. THIS is the table.
  - `value_trust_depth`: DERIVED = shallowest value-leaf depth whose LP is within `--tol` of the converged
                         heuristic (min over hdepths). Omitted (UNSET) if the leaf never converges -> the engine
                         escalates at every depth. (Unchanged semantics; engine reads this.)
  - `value_no_fallback`: DERIVED = the value leaf is so weak that even at its DEEPEST depth it is materially
                         worse than the heuristic the take-crossover would credit it against, so an escalated
                         line must ALWAYS be taken (never keep the leaf line). True iff
                         `V_top - H_{max(top-offset, min_hdepth)} > --margin`. False for a leaf that is >= that
                         heuristic (the five originally-calibrated decks), so their play stays byte-identical.

LP = loss-penalised avg win turn (loss=max_turns+1, lower=better). Writes the minimal-diff compact JSON that the
trained sidecar uses. Run once per value model, right after its depth matrix is generated.

    # backfill the five decks from the committed-quality pure matrix
    scripts/valueleaf_table_to_metadata.py logs/eval/valueleaf_depth_matrix_pure.txt --dry-run
    scripts/valueleaf_table_to_metadata.py logs/eval/valueleaf_depth_matrix_pure.txt
    # hinata, once its (expensive) matrix finishes
    scripts/valueleaf_table_to_metadata.py logs/eval/valueleaf_depth_matrix_hinata.txt --decks hinata
"""
import argparse, collections, glob, json, os, re, sys

# Deck locations come from scripts/deck_registry.py -- pure discovery of decks/*/, no list to maintain.
# This used to be a hand-written NAME2VALUE dict, and an unlisted deck did NOT error: write_deck
# printed "SKIP (no metadata path mapped)" and returned success, so the phase reported done having
# written nothing. FiveColour would have measured 52 cells over ~8 hours and then thrown the
# derivation away, with phase E going on to A/B a model that had no table and no crossover.
#
# A "<deck>_staged" key targets logs/eval/<stem>.value.STAGED.json instead of the live sidecar: a
# regenerated table CHANGES PLAY, so it is measured against the live one before being installed.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import deck_registry

_REG = deck_registry.discover()
NAME2VALUE = {}
for _k, _d in _REG.items():
    NAME2VALUE[_k] = _d.value
    NAME2VALUE[_k + deck_registry.STAGED_SUFFIX] = _d.staged

HDR = re.compile(r"games=(\d+)\s+seeds=\[([^\]]+)\]\s+value_min_depth=(\d+)")
HGAMES = re.compile(r"hgames=(\d+)")
HGAMES_DEPTH = re.compile(r"hgames_depth=([\d:,]+)")
DECK = re.compile(r"^----\s+(\S+)\s+\(mean over (\d+) seeds\)")
# The trailing `*` is emit_table's REFERENCE-ONLY marker (`*=intractable=reference-only`): at least one
# seed-cell of that depth was condemned, so the row is a partial sample, not a measurement. It used to be
# invisible here -- the old pattern stopped at `[` and the marker sits after `]` -- so a condemned row was
# parsed as an ordinary ladder entry. On FiveColour that flipped the derived runtime rule for committed
# depths 6-8 from "never fall back" to "fall back at H6", on the strength of an H6 mean taken from 4 games
# on one seed and 7 on another. The `]` and the marker are optional so pre-marker logs still parse.
HROW = re.compile(r"H(\d+)=(-?[\d.]+)\[[^\]]*\]?(\*?)")
VROW = re.compile(r"V(\d+)=(-?[\d.]+)\[[^\]]*\]?(\*?)")
# The per-deck `# games/cell:` line emit_table already writes, e.g. `H1:300g  H5:325g  H6:4g`. These are
# the REAL per-depth counts (the min over seeds), which is what training_adequacy needs: the `hgames=`
# header field carries args.target, so it declares 400 games for a cell that has 4.
GAMES_CELL = re.compile(r"\b([HV])(\d+):(\d+)g")


def parse_log(path, average=False):
    """Return {deck: block}, MERGING every occurrence of a deck across the log.

    Default merge is per-depth LATEST-WINS (a value-first pass then a targeted heuristic pass compose; and
    across logs the authoritative source is concatenated LAST -> pure d1-5 overrides a coarser extension).

    With average=True, per-depth cells are AVERAGED instead (running mean over all emits of that depth). This
    is for the PER-SEED incremental logs (valueleaf_incremental.py emits one block per (seed,depth) so a
    mid-run cancel keeps every finished seed): the N per-seed emits of a depth are averaged back into the mean.
    Do NOT use average=True for the cross-provenance 5-deck merge (it would blend pure with the extension).

    Each cell carries the games it was measured at (hg_cell[d]/vg_cell[d]) for the training-adequacy check.
    block: {deck, H, V, hg_cell, vg_cell, meta(last), seeds}."""
    blocks = {}
    meta = {"games": None, "seeds": None, "value_min_depth": None, "vgames": None,
            "hgames_default": None, "hgames_depth": {}}
    cur = None
    for line in open(path):
        m = HDR.search(line)
        if m:
            vgames = int(m.group(1))                       # header `games=` carries the value arm's games
            hg = HGAMES.search(line)
            hgd = {}
            hm = HGAMES_DEPTH.search(line)
            if hm:
                for pair in hm.group(1).split(","):
                    k, v = pair.split(":"); hgd[int(k)] = int(v)
            # Old logs (no hgames=) were symmetric full games: heuristic games default to the value games.
            hgames_default = int(hg.group(1)) if hg else vgames
            meta = {"games": vgames, "vgames": vgames,
                    "seeds": [int(s) for s in m.group(2).replace(",", " ").split()],
                    "value_min_depth": int(m.group(3)),
                    "hgames_default": hgames_default, "hgames_depth": hgd}
            continue
        m = DECK.match(line.strip())
        if m:
            name = m.group(1)
            cur = blocks.get(name)
            if cur is None:
                cur = {"deck": name, "H": {}, "V": {}, "hg_cell": {}, "vg_cell": {},
                       "meta": dict(meta), "seeds": meta["seeds"], "ref_only": set(),
                       "_Hsum": {}, "_Hcnt": {}, "_Vsum": {}, "_Vcnt": {}, "_seedset": set()}
                blocks[name] = cur
            cur["_seedset"].update(meta["seeds"])
            cur["meta"] = dict(meta); cur["seeds"] = meta["seeds"]   # newest provenance for the merged block
            cur["_hgd"] = meta["hgames_depth"]; cur["_hgdef"] = meta["hgames_default"]; cur["_vg"] = meta["vgames"]
            continue
        if cur is None:
            continue
        if "heuristic:" in line:
            for d, lp, ref in HROW.findall(line):
                dd = int(d)
                if ref:                                       # reference-only: the row does not exist here
                    cur["ref_only"].add(("H", dd)); continue
                if average:
                    cur["_Hsum"][dd] = cur["_Hsum"].get(dd, 0.0) + float(lp); cur["_Hcnt"][dd] = cur["_Hcnt"].get(dd, 0) + 1
                    cur["H"][dd] = cur["_Hsum"][dd] / cur["_Hcnt"][dd]
                else:
                    cur["H"][dd] = float(lp)
                cur["hg_cell"][dd] = cur["_hgd"].get(dd, cur["_hgdef"])   # games this H cell was measured at
        elif "value-leaf:" in line:
            for d, lp, ref in VROW.findall(line):
                dd = int(d)
                if ref:
                    cur["ref_only"].add(("V", dd)); continue
                if average:
                    cur["_Vsum"][dd] = cur["_Vsum"].get(dd, 0.0) + float(lp); cur["_Vcnt"][dd] = cur["_Vcnt"].get(dd, 0) + 1
                    cur["V"][dd] = cur["_Vsum"][dd] / cur["_Vcnt"][dd]
                else:
                    cur["V"][dd] = float(lp)
                cur["vg_cell"][dd] = cur["_vg"]
        elif "# games/cell:" in line:
            # Real per-depth counts, overwriting the header's declared target. Emitted per arm, per deck,
            # immediately after that arm's row, so the arm letter disambiguates.
            for arm, d, g in GAMES_CELL.findall(line):
                (cur["hg_cell"] if arm == "H" else cur["vg_cell"])[int(d)] = int(g)
    # With averaging, the merged block's seed provenance is the UNION of all per-seed emits seen.
    if average:
        for cur in blocks.values():
            cur["seeds"] = sorted(cur["_seedset"]); cur["meta"] = dict(cur["meta"], seeds=cur["seeds"])
    # Never drop a row silently: an excluded depth SHORTENS the ladder, and the ladder's top is what the
    # crossover's "never fall back" sentinel (max(hd)+1) is measured against.
    for cur in blocks.values():
        if cur["ref_only"]:
            sys.stderr.write("NOTE %s: reference-only rows excluded from the ladder -- %s\n"
                             % (cur["deck"], " ".join("%s%d" % r for r in sorted(cur["ref_only"]))))
    return blocks


def repair_from_cells(blocks, cells_path, log=sys.stderr.write):
    """Recompute every row's LP over ONE GAME SET per seed, from the per-game records.

    THE TABLE'S ROW MEANS ARE NOT ALWAYS COMPARABLE, AND THIS FUNCTION IS WHY THAT MATTERS HERE.
    Everything downstream -- trust_depth, no_fallback, the crossover -- is a COMPARISON between one
    depth's LP and another's, so the two have to be over the same games or the difference is partly
    just which hands each cell drew. The matrix keeps comparable cells aligned (a game abandoned in
    any cell is dropped from all of them), but two kinds of cell legitimately hold less: one capped
    at a reference sample, and one whose rung was capped after being measured EQUIVALENT to the rung
    below. The first is already excluded from the ladder; the second is not, and it is the one whose
    row mean would otherwise silently carry a different population into the derivation.

    Measured cost of not doing this, burn 2026-08-15: H3 over 400 games read 4.3350 and H4 over 360
    read 4.2589 -- an apparent 0.076-turn gain from one extra rung. Over the games both held, the two
    were identical to four decimals. A derivation reading those means would place the crossover a
    rung early on nothing at all.

    Reference-capped (intractable) cells are excluded from the basis rather than intersected into it:
    one 50-game cell would drag every row in the deck down to 50 games.
    """
    try:
        cells = json.load(open(cells_path))
    except (OSError, ValueError) as e:
        log("NOTE per-game repair SKIPPED (%s): rows are the table's own means, which are only\n"
            "     comparable if every cell held the same games -- see emit_table's UNEQUAL GAME SETS guard\n"
            % e)
        return
    def per_game(c):
        return {int(o): (float(w) if w > 0 else None) for x in c.get("chunks", [])
                for o, w in (x.get("g") or ())}
    for name, blk in blocks.items():
        dc = [c for c in cells if c["deck"] == name]
        if not dc: continue
        basis, mt = {}, None
        for seed in sorted({c["seed"] for c in dc}):
            usable = [c for c in dc if c["seed"] == seed and not c.get("intractable")
                      and any(x.get("g") for x in c.get("chunks", []))]
            if usable: basis[seed] = set.intersection(*[set(per_game(c)) for c in usable])
        if not basis: continue
        # max_turns, for the loss penalty -- recovered from the deck registry the same way the
        # matrix driver does, so a loss scores identically on both sides.
        try:
            import deck_registry as _dr
            mt = _dr.discover()[name.replace(_dr.STAGED_SUFFIX, "")].max_turns
        except Exception:
            mt = 8
        moved = []
        for arm, key in (("H", "H"), ("V", "V")):
            for d in sorted(blk[key]):
                if (arm, d) in blk["ref_only"]: continue
                vals, n = [], 0
                for seed, b in basis.items():
                    c = next((x for x in dc if x["seed"] == seed and x["arm"] == arm
                              and x["depth"] == d), None)
                    if c is None: continue
                    g = per_game(c)
                    common = [(g[o] if g[o] is not None else float(mt + 1)) for o in b if o in g]
                    if not common: continue
                    vals.append(sum(common) / len(common)); n = len(common)
                if not vals: continue
                new = sum(vals) / len(vals)
                if abs(new - blk[key][d]) > 5e-5: moved.append((arm, d, blk[key][d], new))
                blk[key][d] = new
                (blk["hg_cell"] if arm == "H" else blk["vg_cell"])[d] = n
                # KEEP the per-game vector, keyed on (seed, offset) so two cells can be paired
                # GAME BY GAME later. `derive` needs it to put an error bar on V_d - h_conv: the
                # difference between two rows is what the trust gate reads, and a row mean alone
                # cannot say whether a 0.0038 gap is a real one or the sampling noise of one cell
                # (see gap_bound).
                blk.setdefault("_pg", {})[(arm, d)] = {
                    (seed, o): (g[o] if g[o] is not None else float(mt + 1))
                    for seed, b in basis.items()
                    for c in [next((x for x in dc if x["seed"] == seed and x["arm"] == arm
                                    and x["depth"] == d), None)] if c
                    for g in [per_game(c)]
                    for o in b if o in g}
        log("NOTE %s: rows recomputed over the PAIRED game set (%s)%s\n"
            % (name, ", ".join("s%d:%dg" % (s, len(b)) for s, b in sorted(basis.items())),
               "" if not moved else
               "; moved " + ", ".join("%s%d %.4f->%.4f" % m for m in moved)))


def gap_bound(pg, a, b):
    """One-sided 95% UPPER bound on the PAIRED gap LP(a) - LP(b), over the games both cells hold.

    WHY THE TRUST GATE NEEDS THIS. `value_trust_depth` decides whether the hybrid may KEEP a
    value-leaf line without escalating it -- the one lever on which a weak leaf can cost quality
    rather than time (docs/design/value-leaf-quality-floor.md). It was decided by a point estimate
    against a hard constant: `V_d - h_conv <= tol`. A point estimate has no idea whether it is
    looking at a real gap or at one cell's sampling noise, so the verdict flip-flops near the
    boundary -- which is exactly what the Knights decline recorded ("the hard 0.002 threshold on a
    noisy cell flip-flops near the boundary; a noise-aware margin, clear tol by > cell SE, is a
    deferred improvement"), and Creature Giving is a second instance at +0.0038 against tol 0.0020.

    So the claim has to be an EQUIVALENCE claim, the same shape the matrix driver's rung
    condemnation uses: the leaf is trusted only when the gap is BOUNDED below tol, not merely
    measured below it. That also makes thin evidence fail SAFE by construction -- fewer games means
    a wider bound means no trust -- which is the direction that cannot cost quality.

    PAIRED, because the difference between two depths is an order of magnitude under the
    between-GAME spread; unpaired, this would mostly measure which hands each cell drew.

    Returns (mean, se, upper, n, resolution) or None when either cell has no per-game record.

    `resolution` = 3*step/n is the rule-of-three bound: the smallest gap this SAMPLE SIZE could ever
    certify, since k=0 differing games in n pairs still admits a differing-game rate up to 3/n and a
    game that does differ moves the score by at least one whole turn. It is REPORTED, not folded into
    the verdict, and the difference matters: the matrix driver's rung test does fold it in, because
    there the question is "is this rung dead" and over-caution merely keeps measuring. Here it would
    make the rule UNREACHABLE -- at tol=0.0020, `3/n < tol` needs n >= 1500 paired games, and a cell
    tops out at 4 seeds x 400 before any skip-list attrition (burn measured 1,445). Folding it in
    turned burn's V6 -- identical to the heuristic on every one of those 1,445 games -- into NOT
    TRUSTED, i.e. it silently flipped a shipped deck to always-escalate on a knife edge. A test that
    can only ever return one answer is not measuring anything. So when tol sits below the sample's
    resolution, the honest move is to SAY the sample cannot settle it and let a human choose between
    a bigger sample and a bigger tol -- not to quietly pick the strict answer.
    """
    ga, gb = pg.get(a), pg.get(b)
    if not ga or not gb: return None
    common = sorted(set(ga) & set(gb))
    n = len(common)
    if n < 2: return None
    diffs = [ga[k] - gb[k] for k in common]
    mean = sum(diffs) / n
    var = sum((d - mean) ** 2 for d in diffs) / (n - 1)
    se = (var / n) ** 0.5
    step = min((abs(d) for d in diffs if d), default=1.0)
    return mean, se, mean + 1.645 * se, n, 3.0 * step / n


def derive(H, V, tol, offset, margin, scalar_cap=None, pg=None, log=None):
    """Derive the PLAY-TIME scalars (h_conv, trust_depth, no_fallback) plus sorted depth lists.

    The scalars use ONLY the in-play depth range (depths <= scalar_cap): play searches to <= this depth and
    escalates to a heuristic that reaches <= this depth, so the escalation gate must compare against the
    in-play heuristic -- NOT a deeper cell. Deep cells (d6-8) are measured at different seeds/games (for the
    anchor / convergence record) and would otherwise drag h_conv across a provenance seam, shifting the gate
    (e.g. slivers trust 5->6). crossover() still spans ALL depths: its c<=cap entries are invariant to deeper
    H (it takes the SHALLOWEST crossing), and its c>cap entries are out-of-play informational."""
    hd = sorted(H); vd = sorted(V)
    hd_s = [d for d in hd if scalar_cap is None or d <= scalar_cap] or hd   # in-play H depths
    vd_s = [d for d in vd if scalar_cap is None or d <= scalar_cap] or vd   # in-play V depths
    h_conv = min(H[d] for d in hd_s)
    # The H depth h_conv came from -- the cell the trust gate is actually comparing against, and the
    # one the paired bound has to be taken over.
    h_conv_d = min(hd_s, key=lambda d: H[d])
    # NOISE-AWARE TRUST (see gap_bound). Trust requires the gap to be BOUNDED below tol, not merely
    # measured below it. Falls back to the old point rule ONLY when there is no per-game record to
    # bound with -- a legacy table cannot be given an error bar retroactively, and silently turning
    # every such deck's trust off would be a behaviour change made on absent evidence rather than on
    # measured evidence.
    trusted = []
    warned = False
    for d in vd_s:
        b = gap_bound(pg or {}, ("V", d), ("H", h_conv_d))
        if b is None:
            if V[d] - h_conv <= tol: trusted.append(d)
            continue
        mean, se, upper, n, resolution = b
        if upper <= tol: trusted.append(d)
        elif log and V[d] - h_conv <= tol:
            # The case this rule exists for: the point estimate says trust, the bound says the
            # evidence does not support it. Report it, because it is a verdict CHANGE.
            log("NOTE V%d point gap %+.4f is within tol=%.4f but its one-sided 95%% upper bound is "
                "%.5f over %d paired games (se %.5f) -- NOT trusted (noise-aware margin)\n"
                % (d, mean, tol, upper, n, se))
        elif log and mean - 1.645 * se <= tol < upper:
            # INCONCLUSIVE, and that is a different verdict from "measured worse". The response to
            # this one is more games; the response to a real gap is to accept UNSET.
            log("NOTE V%d gap %+.4f is INCONCLUSIVE against tol=%.4f: the 90%% interval "
                "[%+.5f, %+.5f] straddles it on %d paired games. UNSET is the safe default, but the "
                "fix here is MORE GAMES, not acceptance.\n"
                % (d, mean, tol, mean - 1.645 * se, upper, n))
        # Once per deck, not per depth: the resolution is a property of the SAMPLE (3/n), so the
        # same line for every V row is noise around a single fact.
        if log and tol < resolution and not warned:
            warned = True
            log("WARN tol=%.4f is BELOW this sample's resolution %.5f (3/n at n=%d paired games) -- "
                "no sample this size can certify a gap that small in either direction. Raise the "
                "sample or the tolerance; a trust verdict at this tol is not evidence.\n"
                % (tol, resolution, n))
    trust_depth = trusted[0] if trusted else None
    top_v = max(vd_s)
    ref_h = max(top_v - offset, min(hd_s))        # depth the take-crossover credits the deepest leaf against
    no_fallback = (V[top_v] - H[ref_h]) > margin
    return h_conv, trust_depth, no_fallback, ref_h, hd, vd, top_v


def monotonicity_flags(H, V, eps=1e-9):
    """Flag any case where a DEEPER search comes out WORSE (higher LP) -- for both the heuristic and the value
    leaf. Deeper should never hurt on average, so a positive delta is either sampling noise (=> more games) or
    genuine search pathology; either way we surface it rather than silently swallowing it via the H envelope.
    Returns a list of human-readable warnings (empty = clean)."""
    out = []
    for name, S in (("H", H), ("V", V)):
        ds = sorted(S)
        for a, b in zip(ds, ds[1:]):
            if S[b] > S[a] + eps:
                out.append("%s%d->%s%d WORSENS by %+.4f (%.4f -> %.4f)" % (name, a, name, b, S[b] - S[a], S[a], S[b]))
    return out


def crossover(H, V):
    """The 'simpler matrix': per leaf-commit depth c, the shallowest heuristic depth hc* that BEATS the leaf
    (H_hc < V_c). At runtime, after escalating a value-leaf line committed at depth c to a heuristic that
    reached hcommitted, TAKE the heuristic iff hcommitted >= hc*[c] (else keep the leaf). This replaces the
    uniform offset-3 proxy: each committed depth gets its own measured fallback level (they are NOT all c-3).
      hc*[c] = 1        -> heuristic beats the leaf even at depth 1  => always fall back (weak leaf)
      hc*[c] = maxH + 1 -> heuristic never beats the leaf            => never fall back (strong leaf)
    Uses a monotone (non-increasing) envelope of H to guard against LP noise inverting adjacent depths."""
    hd = sorted(H); vd = sorted(V)
    hmono = {}; best = float("inf")
    for d in hd:
        best = min(best, H[d]); hmono[d] = best
    res = collections.OrderedDict()
    for c in vd:
        star = next((hc for hc in hd if hmono[hc] < V[c]), None)
        res[c] = star if star is not None else (max(hd) + 1)
    return res


def training_adequacy(H, V, xover, hgames_of, vgames, min_games, band=0.03, conv_eps=0.005):
    """Enforce 'pay the cost unless expensive AND doesn't matter'. A cell H_hc MATTERS for committed depth c if
    it could set c's fallback level -- i.e. H_hc sits near V_c (within `band`, so the keep/take decision there is
    close) OR it brackets the crossing. There is NOT one crossover: several cells (incl. the offset comparison
    like V5-vs-H2) can decide 'what level of fallback is helpful', so we take the whole near-crossing region, not
    just the first crossing. A deep cell that has CONVERGED with its shallower neighbor (|H_hc-H_{hc-1}|<=conv_eps)
    is redundant -- its shallower neighbor already carries the decision -- so it is EXEMPT (this is the sanctioned
    cut: Hinata H4~H3~H5 past a shallow crossing). We warn iff a MATTERS-and-not-converged cell is under-trained,
    plus if the (always-decisive) value arm is under-trained. Returns (warnings, per_c_detail)."""
    hd = sorted(H); vd = sorted(V); maxh = max(hd); minh = min(hd)
    Hprev = {d: (H[hd[i - 1]] if i > 0 else None) for i, d in enumerate(hd)}
    def converged(d):  # deeper cell barely improves on its shallower neighbor -> redundant
        return Hprev[d] is not None and abs(H[d] - Hprev[d]) <= conv_eps
    warns = []; detail = collections.OrderedDict()
    if vgames is not None and vgames < min_games:
        warns.append("VALUE arm under-trained (%d < %d games): every crossover depends on V -- pay for it"
                     % (vgames, min_games))
    for c in vd:
        star = xover[c]
        matters = set()
        for hc in hd:
            if abs(H[hc] - V[c]) <= band:        # near a plausible crossing for this c
                matters.add(hc)
        if star <= maxh:                          # the actual first-beat and last-fail bracket
            matters.add(star)
            if star - 1 >= minh: matters.add(star - 1)
        else:
            matters.add(maxh)                     # 'never fall back' rests on the deepest H not beating V_c
        # exempt converged-redundant deep cells (unless a cell is the SOLE near-crossing one, keep it)
        essential = {hc for hc in matters if not converged(hc)}
        if not essential: essential = {min(matters)}   # keep the shallowest as the decision cell
        under = [(d, hgames_of(d)) for d in sorted(essential) if hgames_of(d) < min_games]
        detail[c] = {"take_at": star, "matters_hdepths": sorted(essential),
                     "matters_games": {d: hgames_of(d) for d in sorted(essential)},
                     "exempt_converged": sorted(matters - essential)}
        for d, g in under:
            warns.append("c=%d fallback MATTERS at H%d (near V%d=%.4f) but H%d under-trained (%d<%d games): pay for it"
                         % (c, d, c, V[c], d, g, min_games))
    return warns, detail


def completeness_error(deck, V, trust_depth, no_fallback, h_conv, tol, target_depth, conv_eps=0.005):
    """Return a human-readable reason string iff this table is INCONCLUSIVE -- i.e. it is NOT safe to write
    the derived scalars because the value ladder was truncated BELOW the play depth, so an UNSET trust could
    not have been earned. Returns None (OK to write) when the table is conclusive.

    The ONLY thing the escalation gate reads is whether the leaf reaches `tol` of h_conv WITHIN the in-play
    depth range (depths <= scalar_cap == the deck's target_depth). So a table is conclusive as long as that
    in-play range is fully measured:
      - trust_depth SET  -> conclusive (leaf trusted from that depth).
      - no_fallback True -> conclusive (leaf provably worse everywhere -> always escalate-and-take).
      - trust UNSET but max(vdepths) >= target_depth -> conclusive UNSET: the leaf at the play depth is
        genuinely > tol worse than the heuristic, so escalate-and-TAKE is correct (hinata/dragonstorm/
        antilife/TH). Whether the leaf keeps descending at OUT-OF-CAP depths (d > target_depth) does NOT
        move the in-play gate, so it is irrelevant here.
    INCONCLUSIVE iff trust UNSET *and* not no_fallback *and* max(vdepths) < target_depth: the ladder stops
    BELOW the play depth, so an unmeasured IN-RANGE cell could still cross tol and set a real trust depth.
    This is exactly the 2026-07 goblins bug (V ladder stopped at V5 while target_depth was 6, so V6 -- the
    in-range cell that crosses tol -- was never measured, trust came out UNSET, and the engine
    escalated-and-discarded at the play depth). The earlier version of this guard ALSO fired on a merely
    'still-descending' deepest cell that was AT the cap (hinata/dragonstorm, measured to V5 == target 5);
    that was a false positive -- descending out-of-range does not change the gate."""
    vdall = sorted(V)
    if trust_depth is not None or no_fallback or not target_depth:
        return None
    if max(vdall) >= target_depth:
        return None
    detail = ""
    if len(vdall) >= 2 and V[vdall[-1]] < V[vdall[-2]] - conv_eps:
        detail = (" and V%d=%.4f is still DESCENDING from V%d=%.4f (gap to h_conv=%.4f is %.4f), so a deeper "
                  "IN-RANGE cell would likely cross tol=%.4f and set a real trust depth"
                  % (vdall[-1], V[vdall[-1]], vdall[-2], V[vdall[-2]], h_conv, V[vdall[-1]] - h_conv, tol))
    return ("value_trust_depth is UNSET but the value ladder tops out at V%d, SHORT of the play "
            "target_depth=%d -- the in-play depth range is not fully measured%s. Extend --vdepths to >= %d "
            "and re-measure (or pass --allow-partial if this UNSET is intentional)."
            % (max(vdall), target_depth, detail, target_depth))


def write_deck(block, tol, offset, margin, dry, scalar_cap=None, set_esc_cap=True, allow_partial=False):
    deck = block["deck"]
    prof = NAME2VALUE.get(deck)
    if prof is None:
        print("  %-9s SKIP (no metadata path mapped)" % deck); return True
    H, V, meta = block["H"], block["V"], block["meta"]
    if not H or not V:
        print("  %-9s SKIP (log block missing H or V rows)" % deck); return True
    # Resolve the play target_depth from the EXISTING profile (used to auto-cap the scalars AND to guard
    # ladder coverage). Auto-cap = the deck's search depth: the shipped default of 5 silently under-capped
    # target_depth=6 decks (burn/goblins), excluding V6 from the scalar derivation -- part of the goblins bug.
    _ep = json.load(open(prof), object_pairs_hook=collections.OrderedDict)
    _vp = _ep.get("value_play") if isinstance(_ep.get("value_play"), dict) else {}
    target_depth = _vp.get("target_depth")
    if scalar_cap is None:
        scalar_cap = int(target_depth) if target_depth else 5
    h_conv, trust_depth, no_fallback, ref_h, hd, vd, top_v = derive(
        H, V, tol, offset, margin, scalar_cap,
        pg=block.get("_pg"), log=lambda s: sys.stderr.write("  %-9s %s" % (deck, s)))
    # Re-run the bounds purely to RECORD them (derive() consumes them for the verdict).
    _hcd = min([d for d in sorted(H) if scalar_cap is None or d <= scalar_cap] or sorted(H),
               key=lambda d: H[d])
    trust_ev = collections.OrderedDict()
    for d in [x for x in sorted(V) if scalar_cap is None or x <= scalar_cap] or sorted(V):
        b = gap_bound(block.get("_pg") or {}, ("V", d), ("H", _hcd))
        trust_ev["V%d" % d] = (
            collections.OrderedDict([("gap", round(b[0], 5)), ("se", round(b[1], 5)),
                                     ("upper95", round(b[2], 5)), ("paired_games", b[3]),
                                     ("sample_resolution", round(b[4], 5))])
            if b else
            collections.OrderedDict([("gap", round(V[d] - h_conv, 5)), ("se", None),
                                     ("upper95", None), ("paired_games", None),
                                     ("note", "no per-game record: point test only")]))
    problem = completeness_error(deck, V, trust_depth, no_fallback, h_conv, tol, target_depth)
    if problem and not allow_partial:
        print("  %-9s ✗ REFUSING TO WRITE (incomplete/inconclusive table): %s" % (deck, problem))
        return False
    if problem:
        print("  %-9s ⚠ writing PARTIAL table (--allow-partial): %s" % (deck, problem))
    xover = crossover(H, V)   # {committed_depth c: hc*[c]} -- the DERIVED runtime fallback rule
    warns = monotonicity_flags(H, V)   # deeper-is-worse anomalies (noise or search pathology)

    # Preserve MANUAL crossover overrides across regeneration. These correct a measured cell whose
    # table-derived hc* does not transfer to PLAY (e.g. the value leaf undervalues a specific enabler), WITHOUT
    # touching value_leaf_table (which stays a faithful measurement). Stored under value_fallback_crossover.
    # manual_overrides = {"<committed_depth>": {"from": <derived>, "to": <forced>, "reason": "..."}}. The engine
    # reads take_heuristic_at_hdepth (= derived with overrides applied); derived_take_heuristic_at_hdepth keeps
    # the untouched derivation visible for audit. Empty overrides => byte-identical to before (no extra keys).
    existing_prof = _ep
    ex_xo = existing_prof.get("value_fallback_crossover")
    manual = (ex_xo.get("manual_overrides") if isinstance(ex_xo, dict) else None) or {}
    cdepths = list(xover.keys())
    derived_vals = list(xover.values())
    effective_vals = list(derived_vals)
    for cs, ov in manual.items():
        c = int(cs)
        if c in cdepths:
            effective_vals[cdepths.index(c)] = int(ov["to"] if isinstance(ov, dict) else ov)

    table = collections.OrderedDict([
        ("metric", "loss_penalized_avg_win_turn"),
        ("value_min_depth", meta["value_min_depth"]),
        ("games", meta["games"]),
        ("seeds", meta["seeds"]),
        ("hdepths", hd),
        ("heuristic_lp", [round(H[d], 4) for d in hd]),
        ("vdepths", vd),
        ("value_leaf_lp", [round(V[d], 4) for d in vd]),
        ("h_conv", round(h_conv, 4)),
        ("h_conv_depth_cap", scalar_cap),
        ("tol", tol),
        ("crossover_offset", offset),
        ("no_fallback_margin", margin),
        ("monotonicity_warnings", warns),
        # The trust gate's EVIDENCE, per in-play value depth: the paired gap to the h_conv cell, its
        # standard error, the one-sided 95% upper bound the rule actually tests, and the number of
        # paired games behind it. Recorded because the scalar alone cannot be audited -- a reader
        # otherwise cannot tell a trust UNSET that measured a real gap from one that ran out of
        # evidence, and those call for opposite responses (accept it vs. measure more games).
        ("trust_gap_bounds", trust_ev),
        ("derivation",
         "PLAY SCALARS use only in-play depths (<= h_conv_depth_cap): h_conv = min heuristic_lp over those; "
         "value_trust_depth = min{d<=cap: the PAIRED gap value_leaf_lp[d]-h_conv is BOUNDED below tol, i.e. "
         "its one-sided 95% upper bound <= tol} -- an equivalence claim, so thin or noisy evidence fails "
         "SAFE (no trust => always eligible to escalate), which is the direction that cannot cost quality. "
         "A table with no per-game record falls back to the old point test value_leaf_lp[d]-h_conv <= tol. "
         "value_no_fallback still uses the point rule. "
         "(Deep cells d>cap extend the table+crossover for the anchor but do NOT move the escalation gate; "
         "a trust_depth > search_depth is equivalent to UNSET in play.) crossover spans ALL depths."),
    ])

    # The 'simpler matrix' the engine reads at runtime: per committed depth, the heuristic depth at/above
    # which the escalated line is TAKEN (i.e. we fall back off the leaf). max_search_depth records that the
    # table only covers <= this depth (search deeper than this needs the matrix extended; engine clamps).
    fb = collections.OrderedDict([
        ("rule", "after escalating a value-leaf line committed at depth c, TAKE the heuristic line iff "
                 "hcommitted >= take_heuristic_at_hdepth[c] (else keep the leaf). Clamp c/hcommitted to the "
                 "measured [min,max] depth. Per-committed-depth (NOT a uniform offset)."),
        ("committed_depths", cdepths),
        ("take_heuristic_at_hdepth", effective_vals),
        ("max_table_depth", max(hd + vd)),
    ])
    if manual:
        fb["derived_take_heuristic_at_hdepth"] = derived_vals
        fb["manual_overrides"] = manual

    d = existing_prof
    nd = collections.OrderedDict()
    nd["value_leaf_table"] = table
    nd["value_fallback_crossover"] = fb
    # TRUST IS PROPOSED HERE AND DECIDED BY GAMES (user, 2026-08-15): "how we should be handling
    # trust is by playing with it A/B on vs off in additional games and verifying that the results
    # are good. So the tolerance here would just gate an acceptance test."
    #
    # So the matrix no longer SHIPS `value_trust_depth`; it names a CANDIDATE. The table is a
    # measurement of two arms run separately, and trust is a claim about what happens when the
    # hybrid keeps a leaf line inside real, budgeted play -- which is not the same experiment. The
    # tolerance's job is now to decide whether a candidate is worth spending games on, not to settle
    # it. `valueleaf.sh` phase E runs trustON vs trustOFF on fresh seeds and promotes the candidate
    # into `value_trust_depth` (in the STAGED file -- still not adoption) only if it clears the
    # acceptance test; see scripts/vlq_trust_accept.py.
    #
    # Until that runs the staged model carries NO trust, which is the safe side: every unverified
    # line stays eligible to escalate. A regeneration of a deck that currently ships trust therefore
    # has to re-earn it, deliberately -- the old value was derived under the same unverified rule.
    if trust_depth is not None:
        nd["value_trust_depth_candidate"] = trust_depth
    nd["value_no_fallback"] = no_fallback
    for k, v in d.items():
        if k not in ("value_leaf_table", "value_fallback_crossover", "value_trust_depth",
                     "value_trust_depth_candidate", "value_no_fallback"):
            nd[k] = v

    # AUTO-DERIVE the single-depth escalation cap. The heuristic escalation runs ONE predicted-affordable pass
    # (a cheap frozen-R hint picks the start depth) with a LIVE per-decision up-climb / down-fallback that adapts
    # the actual depth from this decision's measured cost -- deterministic AND adaptive. The right CEILING is the
    # deck's SEARCH depth (value_play.target_depth): the climb handles the real depth adaptivity, so the cap only
    # needs to not truncate below the search depth (capping at the offline heuristic-convergence depth measurably
    # regressed antilife -- the in-play hybrid uses deeper passes than pure heuristic_lp converges at). R defaults
    # to 120 and the climb defaults ON in the engine, so this single integer is the whole per-deck config. 0/off =
    # legacy ladder. Verified quality-neutral (dLP~0) + 20-38% less escalation work on all 6 decks; see
    # docs/design/escalation-beam-verify.md.
    if set_esc_cap:
        vp = nd.get("value_play")
        if isinstance(vp, dict) and vp.get("target_depth"):
            # CLAMP to the deepest MEASURED heuristic depth (user, 2026-08-14). The cap tracks
            # target_depth, but arming escalation PAST the ladder buys nothing the crossover can
            # reward: it clamps hcommitted to the measured [min,max], so a deeper pass is either taken
            # at the clamped depth or not at all -- while still spending budget getting there, which
            # is not byte-neutral inside a bounded search. With the H ladder now topping at H5 (H6
            # dropped: never completed on any deck, see docs/design/depth-matrix-degenerate-games.md)
            # a target_depth of 6 would otherwise arm escalation to a depth we have no measurement for.
            # Binds only where the ladder is SHORTER than the play depth: burn/knights are unchanged.
            vp["escalation_cap"] = min(int(vp["target_depth"]), max(hd))

    td = "UNSET" if trust_depth is None else "%d?(candidate)" % trust_depth
    ov_note = "" if not manual else "  [manual: %s]" % ",".join(
        "c%s:%s->%s" % (k, (v.get("from") if isinstance(v, dict) else "?"),
                        (v.get("to") if isinstance(v, dict) else v)) for k, v in manual.items())
    print("  %-9s trust=%s no_fb=%s  crossover(c->take@hc): %s%s%s"
          % (deck, td, no_fallback,
             " ".join("%d->%s" % (c, hc) for c, hc in zip(cdepths, effective_vals)),
             ov_note, "" if dry else "  [-> %s]" % prof))
    for w in warns:
        print("    ⚠ NON-MONOTONIC: %s" % w)
    if not dry:
        # Match the trained sidecar's compact single-line format (default separators) so the diff is minimal.
        json.dump(nd, open(prof, "w"))
    return True


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="a DEPTH MATRIX log from valueleaf_depth_matrix.py")
    ap.add_argument("--decks", nargs="+", default=None, help="subset (default: every deck found in the log)")
    ap.add_argument("--tol", type=float, default=0.002, help="max LP gap V_d-h_conv to call the leaf trusted")
    ap.add_argument("--offset", type=int, default=3,
                    help="take-crossover offset: the deepest leaf (d) is credited against heuristic at d-offset")
    ap.add_argument("--margin", type=float, default=0.02,
                    help="min LP by which V_top must exceed its credited heuristic to force no_fallback")
    ap.add_argument("--scalar-max-depth", type=int, default=None,
                    help="cap for the PLAY scalars (h_conv/trust_depth/no_fallback): use only depths <= this "
                         "(the in-play search depth). Deeper cells still extend the table+crossover but do not "
                         "move the escalation gate. DEFAULT: auto = the deck's value_play.target_depth (falls "
                         "back to 5). Auto-capping stops the old default of 5 from silently under-capping "
                         "target_depth=6 decks and excluding V6 from the derivation.")
    ap.add_argument("--allow-partial", action="store_true",
                    help="downgrade the completeness guard from an ERROR to a warning and write anyway. The "
                         "guard REFUSES to write a table whose value ladder was truncated before it converged "
                         "(trust UNSET + not no_fallback + deepest V still descending) or that does not reach "
                         "the deck's play target_depth. Use ONLY when the UNSET/partial state is intentional.")
    ap.add_argument("--average-seeds", action="store_true",
                    help="AVERAGE per-depth cells instead of latest-wins -- for PER-SEED incremental logs "
                         "(valueleaf_incremental.py emits one block per (seed,depth)). Do NOT use for the "
                         "cross-provenance 5-deck merge.")
    ap.add_argument("--no-escalation-cap", action="store_true",
                    help="do NOT auto-set value_play.escalation_cap = target_depth (leave the single-depth "
                         "escalation OFF / legacy ladder). Default: auto-enable it (climb-adaptive, quality-neutral).")
    ap.add_argument("--cells", default=None,
                    help="per-game records from the matrix run (default: <log>.cells.json when it exists). "
                         "Every row is recomputed over ONE game set per seed from these, because the "
                         "derivation is entirely COMPARISONS between depths and the table's row means are "
                         "only comparable when every cell held the same games -- a rung capped after being "
                         "measured equivalent holds fewer, and reading its mean placed the crossover a rung "
                         "early on a 0.076-turn difference that was purely which hands each cell drew.")
    ap.add_argument("--dry-run", action="store_true", help="print the derivation but do NOT write")
    args = ap.parse_args()

    blocks = parse_log(args.log, average=args.average_seeds)
    cells_path = args.cells or (args.log + ".cells.json")
    if blocks and os.path.exists(cells_path):
        repair_from_cells(blocks, cells_path)
    elif blocks:
        sys.stderr.write("NOTE no per-game records at %s -- rows are the table's own means. They are "
                         "comparable only if every cell held the same games.\n" % cells_path)
    if not blocks:
        print("no deck blocks parsed from %s" % args.log); sys.exit(1)
    want = args.decks or list(blocks)
    print("=== value-leaf table -> metadata  (tol=%.4f offset=%d margin=%.3f%s) from %s ==="
          % (args.tol, args.offset, args.margin, "  [DRY-RUN]" if args.dry_run else "", args.log))
    ok = True
    for deck in want:
        if deck not in blocks:
            print("  %-9s SKIP (not in log)" % deck); continue
        ok &= write_deck(blocks[deck], args.tol, args.offset, args.margin, args.dry_run, args.scalar_max_depth,
                         set_esc_cap=not args.no_escalation_cap, allow_partial=args.allow_partial)
    if not ok:
        print("REFUSED to write one or more decks (incomplete/inconclusive table). "
              "Extend the ladder and re-measure, or pass --allow-partial if the state is intentional.")
        sys.exit(2)


if __name__ == "__main__":
    main()
