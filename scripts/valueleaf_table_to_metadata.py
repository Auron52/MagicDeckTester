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
import argparse, collections, json, re, sys

# deck name (as printed in the matrix log) -> its model metadata file
NAME2VALUE = {
    "antilife": "decks/Anti-Lifegain/Anti-Lifegain.value.json",
    "slivers":  "decks/slivers_vial/slivers_vial.value.json",
    "TH":       "decks/treasure_hunt/treasure_hunt.value.json",
    "burn":     "decks/burn/burn.value.json",
    "knights":  "decks/Knights/Knights.value.json",
    "hinata":   "decks/Hinata2/Hinata2.value.json",
}

HDR = re.compile(r"games=(\d+)\s+seeds=\[([^\]]+)\]\s+value_min_depth=(\d+)")
HGAMES = re.compile(r"hgames=(\d+)")
HGAMES_DEPTH = re.compile(r"hgames_depth=([\d:,]+)")
DECK = re.compile(r"^----\s+(\S+)\s+\(mean over (\d+) seeds\)")
HROW = re.compile(r"H(\d+)=(-?[\d.]+)\[")
VROW = re.compile(r"V(\d+)=(-?[\d.]+)\[")


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
                       "meta": dict(meta), "seeds": meta["seeds"],
                       "_Hsum": {}, "_Hcnt": {}, "_Vsum": {}, "_Vcnt": {}, "_seedset": set()}
                blocks[name] = cur
            cur["_seedset"].update(meta["seeds"])
            cur["meta"] = dict(meta); cur["seeds"] = meta["seeds"]   # newest provenance for the merged block
            cur["_hgd"] = meta["hgames_depth"]; cur["_hgdef"] = meta["hgames_default"]; cur["_vg"] = meta["vgames"]
            continue
        if cur is None:
            continue
        if "heuristic:" in line:
            for d, lp in HROW.findall(line):
                dd = int(d)
                if average:
                    cur["_Hsum"][dd] = cur["_Hsum"].get(dd, 0.0) + float(lp); cur["_Hcnt"][dd] = cur["_Hcnt"].get(dd, 0) + 1
                    cur["H"][dd] = cur["_Hsum"][dd] / cur["_Hcnt"][dd]
                else:
                    cur["H"][dd] = float(lp)
                cur["hg_cell"][dd] = cur["_hgd"].get(dd, cur["_hgdef"])   # games this H cell was measured at
        elif "value-leaf:" in line:
            for d, lp in VROW.findall(line):
                dd = int(d)
                if average:
                    cur["_Vsum"][dd] = cur["_Vsum"].get(dd, 0.0) + float(lp); cur["_Vcnt"][dd] = cur["_Vcnt"].get(dd, 0) + 1
                    cur["V"][dd] = cur["_Vsum"][dd] / cur["_Vcnt"][dd]
                else:
                    cur["V"][dd] = float(lp)
                cur["vg_cell"][dd] = cur["_vg"]
    # With averaging, the merged block's seed provenance is the UNION of all per-seed emits seen.
    if average:
        for cur in blocks.values():
            cur["seeds"] = sorted(cur["_seedset"]); cur["meta"] = dict(cur["meta"], seeds=cur["seeds"])
    return blocks


def derive(H, V, tol, offset, margin, scalar_cap=None):
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
    trusted = [d for d in vd_s if V[d] - h_conv <= tol]
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


def write_deck(block, tol, offset, margin, dry, scalar_cap=5, set_esc_cap=True):
    deck = block["deck"]
    prof = NAME2VALUE.get(deck)
    if prof is None:
        print("  %-9s SKIP (no metadata path mapped)" % deck); return
    H, V, meta = block["H"], block["V"], block["meta"]
    if not H or not V:
        print("  %-9s SKIP (log block missing H or V rows)" % deck); return
    h_conv, trust_depth, no_fallback, ref_h, hd, vd, top_v = derive(H, V, tol, offset, margin, scalar_cap)
    xover = crossover(H, V)   # {committed_depth c: hc*[c]} -- the DERIVED runtime fallback rule
    warns = monotonicity_flags(H, V)   # deeper-is-worse anomalies (noise or search pathology)

    # Preserve MANUAL crossover overrides across regeneration. These correct a measured cell whose
    # table-derived hc* does not transfer to PLAY (e.g. the value leaf undervalues a specific enabler), WITHOUT
    # touching value_leaf_table (which stays a faithful measurement). Stored under value_fallback_crossover.
    # manual_overrides = {"<committed_depth>": {"from": <derived>, "to": <forced>, "reason": "..."}}. The engine
    # reads take_heuristic_at_hdepth (= derived with overrides applied); derived_take_heuristic_at_hdepth keeps
    # the untouched derivation visible for audit. Empty overrides => byte-identical to before (no extra keys).
    existing_prof = json.load(open(prof), object_pairs_hook=collections.OrderedDict)
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
        ("derivation",
         "PLAY SCALARS use only in-play depths (<= h_conv_depth_cap): h_conv = min heuristic_lp over those; "
         "value_trust_depth = min{d<=cap: value_leaf_lp[d]-h_conv <= tol}; value_no_fallback likewise. "
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
    if trust_depth is not None:
        nd["value_trust_depth"] = trust_depth
    nd["value_no_fallback"] = no_fallback
    for k, v in d.items():
        if k not in ("value_leaf_table", "value_fallback_crossover", "value_trust_depth", "value_no_fallback"):
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
            vp["escalation_cap"] = int(vp["target_depth"])

    td = "UNSET" if trust_depth is None else str(trust_depth)
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("log", help="a DEPTH MATRIX log from valueleaf_depth_matrix.py")
    ap.add_argument("--decks", nargs="+", default=None, help="subset (default: every deck found in the log)")
    ap.add_argument("--tol", type=float, default=0.002, help="max LP gap V_d-h_conv to call the leaf trusted")
    ap.add_argument("--offset", type=int, default=3,
                    help="take-crossover offset: the deepest leaf (d) is credited against heuristic at d-offset")
    ap.add_argument("--margin", type=float, default=0.02,
                    help="min LP by which V_top must exceed its credited heuristic to force no_fallback")
    ap.add_argument("--scalar-max-depth", type=int, default=5,
                    help="cap for the PLAY scalars (h_conv/trust_depth/no_fallback): use only depths <= this "
                         "(the in-play search depth). Deeper cells still extend the table+crossover but do not "
                         "move the escalation gate. Default 5 (shipped search depth).")
    ap.add_argument("--average-seeds", action="store_true",
                    help="AVERAGE per-depth cells instead of latest-wins -- for PER-SEED incremental logs "
                         "(valueleaf_incremental.py emits one block per (seed,depth)). Do NOT use for the "
                         "cross-provenance 5-deck merge.")
    ap.add_argument("--no-escalation-cap", action="store_true",
                    help="do NOT auto-set value_play.escalation_cap = target_depth (leave the single-depth "
                         "escalation OFF / legacy ladder). Default: auto-enable it (climb-adaptive, quality-neutral).")
    ap.add_argument("--dry-run", action="store_true", help="print the derivation but do NOT write")
    args = ap.parse_args()

    blocks = parse_log(args.log, average=args.average_seeds)
    if not blocks:
        print("no deck blocks parsed from %s" % args.log); sys.exit(1)
    want = args.decks or list(blocks)
    print("=== value-leaf table -> metadata  (tol=%.4f offset=%d margin=%.3f%s) from %s ==="
          % (args.tol, args.offset, args.margin, "  [DRY-RUN]" if args.dry_run else "", args.log))
    for deck in want:
        if deck not in blocks:
            print("  %-9s SKIP (not in log)" % deck); continue
        write_deck(blocks[deck], args.tol, args.offset, args.margin, args.dry_run, args.scalar_max_depth,
                   set_esc_cap=not args.no_escalation_cap)


if __name__ == "__main__":
    main()
