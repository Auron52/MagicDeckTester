#!/usr/bin/env python3
"""Full value-leaf x heuristic DEPTH MATRIX (UNBOUNDED) with cost, to (1) separate decks where the value
leaf is ~EXACT (no heuristic needed) from those that GENUINELY need the heuristic, and (2) calibrate the
per-model "trust depth": at what value-leaf depth does it match heuristic-at-D?

CRITICAL (2026-07-11 fix): measures the PURE value-leaf by DISABLING the hybrid redo (--value-min-depth 0,
env MTG_VALUE_MIN_DEPTH=0). The earlier run used MIN_DEPTH=5, which made every committed depth < 5 RE-RUN
the heuristic -- so V3/V4 were the HYBRID (heuristic on the leaf-dependent games) and only V5 was the raw
leaf. That confound produced the spurious "value-leaf exact at d3/d4, worse only at d5" + the impossible
"V4 beats V5" (a deeper search cannot be worse unless d3/d4 were secretly the heuristic). With MIN_DEPTH=0
every V_d is the raw value-leaf at depth d, so the true crossover (where the heuristic wins) is visible.

All UNBOUNDED (budget 0) so each config reaches its nominal depth. Per deck/seed runs heuristic (value OFF)
at each --hdepths and value-leaf (value ON, MIN_DEPTH=<flag>/a8) at each --vdepths; records LP (loss=mt+1,
lower=better) and wall ms/game. Prints per-deck means + the Vi-Hj difference matrix (negative = value-leaf
better). The binary default is value-ON, so the OFF arm sets MTG_VALUE_MODEL=0 EXPLICITLY. Include low
--vdepths (1,2) to see the leaf-dominated regime where the heuristic's full rollout should win.
See learned-d0-policy.md.

    scripts/valueleaf_depth_matrix.py --games 1000 --seeds 8008 9009 10010 11011 \
        --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 --value-min-depth 0
"""
import argparse, glob, json, os, re, subprocess, sys, threading, time
from concurrent.futures import ThreadPoolExecutor, wait, FIRST_COMPLETED

MTG = "build/Release/mtg"

# Deck locations come from scripts/deck_registry.py -- pure discovery of decks/*/, no list to maintain.
# There used to be two hand-written dicts here (DECKS and PROFILES) plus an auto-discovery pass that
# patched around them. An unlisted deck did not error: the H-cell ladder is guarded on
# os.path.exists(<value.json>), so a missing entry quietly ran every H cell on the slow path.
#
# A "<deck>_staged" key is the same decklist and the same profile pointing at a model that is NOT yet
# installed in the deck folder (logs/eval/<stem>.value.STAGED.json); the model reaches the engine via
# MTG_VALUE_PROFILE. That is the order the pipeline requires -- measure, then adopt. H cells set
# MTG_VALUE_MODEL=0, so a staged key's H cells are byte-identical to the plain key's; only V moves.
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import deck_registry

_REG = deck_registry.discover()
# key -> (decklist, value-model path, max_turns).  PROFILES is the same keys -> the deck profile.
# Attaching the profile is not optional: measuring profile-less silently describes a deck we do not
# ship, which invalidated every value-leaf table in this repo once already.
DECKS, PROFILES = {}, {}
for _k, _d in _REG.items():
    DECKS[_k] = (_d.deck_file, _d.value, _d.max_turns)
    DECKS[_k + deck_registry.STAGED_SUFFIX] = (_d.deck_file, _d.staged, _d.max_turns)
    PROFILES[_k] = _d.profile
    PROFILES[_k + deck_registry.STAGED_SUFFIX] = _d.profile


# NOTE: the single-invocation helper `run()` that used to live here is DELETED. It ran ONE `mtg` per
# (deck, depth, seed) and was the shared engine of two loop-shaped drivers -- the monolithic branch of
# this file and scripts/attic/valueleaf_incremental.py -- both of which are now gone. Anything that
# needs the matrix goes through run_incremental() -> run_pool(): one manifest, one queue, one tail.

# ==================== INCREMENTAL / TRACTABILITY-AWARE MODE (--incremental) ====================
# The way to build a depth table on decks with a heavy search tail (antilife unbounded deep search has games
# that explode into multi-hour trees). Instead of one monolithic run per (seed,depth) that only reports at the
# very END, sweep EVERY cell in small (50-game) BATCHES, round-robin / breadth-first, so the WHOLE table exists
# at 50 games, then 100, ... Each batch is written the instant it lands (resumable, stoppable, ZERO loss).
#   - TRACTABILITY as we go: from a cell's first (and each) batch, if it is too slow to be production-usable at
#     that depth (sec/game over a threshold), mark it INTRACTABLE and cap it at a small REFERENCE sample -- it
#     still gets a table value, we just don't burn compute pushing an unusable cell to the full target.
#   - WORK-STEALING pool: a cell stuck on a long-tail game ties up ONE core while the rest keep churning other
#     cells' batches, so the box stays full and results keep streaming.
# Batches reproduce a full run EXACTLY: games [off,off+batch) via `--seed (seed+off) --game-index off` (seed_gi
# = seed+global_i, spawn = global_i), verified batched==monolithic. NOTE each concurrent worker is a SEPARATE
# mtg process that loads the deck's keep model; antilife is ~1GB/process (post the share+stream-parse memory
# fix, 82859f7), so --workers 24 ~ 24GB -- was ~6GB/proc before the fix (would OOM). Tune --workers to RAM.

_slow_lock = threading.Lock()   # run_batch is called from a thread pool


def run_batch(deck_file, mt, depth, seed, offset, batch, value_on, value_min_depth, prof,
              deck_profile=None):
    """Run global games [offset, offset+batch) for this cell's base `seed`. Returns (lp, wall_s, games)."""
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"]="1"; env["MTG_VALUE_PROFILE"]=prof
        env["MTG_VALUE_MIN_DEPTH"]=str(value_min_depth); env["MTG_VALUE_STARTGATE_ALPHA"]="8"
    else:
        env["MTG_VALUE_MODEL"]="0"
        # H CELL, LADDERED ON THE CHEAP LEAF. The committed pass is still pure heuristic
        # (MTG_VALUE_MODEL=0); only the ladder's warm-up passes use the value leaf, and their results
        # provably cannot reach the committed line -- verified 21/21 cells byte-identical on avg AND
        # play digest, plus 14 cells avg-identical under the MTG_PROFILE counters. Costs 1.35x-84.8x
        # less search work, most of it at d5, which is the cell this table could never afford.
        # Attaching the profile is required: without a model the warm-up passes have nothing cheap to
        # fall back to and this silently reverts to the slow path (a perf cliff, not a wrong number).
        # Cells run unbounded, which is the regime the identity holds in. See
        # docs/design/value-leaf-regeneration-queue.md 8.3(3).
        if prof and os.path.exists(prof):
            env["MTG_VALUE_PROFILE"]=prof
            env["MTG_LADDER_VALUE_LEAF"]="1"
    cmd=[MTG, deck_file, "--seed", str(seed+offset), "--game-index", str(offset), "--games", str(batch),
         "--max-turns", str(mt), "--threads", "1", "--ignore-play-profile", "--depth", str(depth)]
    if deck_profile: cmd += ["--profile", deck_profile]
    t0=time.time(); _cp=subprocess.run(cmd,capture_output=True,text=True,env=env); out=_cp.stdout; wall=time.time()-t0
    # SLOW-GAME capture. The engine reports any game over MTG_SLOW_GAME_MS on STDERR, which this
    # function used to discard -- so a matrix run knew a CELL was slow but never which GAME. Each
    # line already carries a self-contained repro; tag it with the cell so a pathological game can be
    # attributed to an arm/depth/seed and replayed directly. Append-only, so a resumed run accumulates.
    if os.environ.get("MTG_SLOW_GAME_MS") and _cp.stderr:
        arm = "V" if value_on else "H"
        rows = [l for l in _cp.stderr.splitlines() if "SLOW-GAME" in l]
        if rows:
            with _slow_lock:
                with open(os.environ.get("MTG_SLOW_GAME_LOG", "logs/eval/slow_games.log"), "a") as fh:
                    for l in rows:
                        fh.write("%s%d seed=%d  %s\n" % (arm, depth, seed, l.strip()))
    pm=re.search(r"Games played\s*:\s*(\d+)",out); p=int(pm.group(1)) if pm else 0
    m=re.search(r"avg \(turns\)\s*:\s*([\d.]+)",out); lp=float(m.group(1)) if m else float("nan")
    return lp, wall, p


def _cell_key(c): return "%s|%s|%d|%d" % (c["deck"], c["arm"], c["depth"], c["seed"])


# ==================== CHUNK-LEVEL STATE (the unit of consistency) ====================
# A cell's results are stored as the LIST OF CHUNKS that produced them, not as a running total:
#
#     {"off": 375, "n": 25, "lp": 5.31, "ms": 9508.2, "src": "<tree hash of src/>"}
#
# Games are addressed by OFFSET, and the offset is what fixes identity: chunk [off,off+n) always runs
# `--seed (seed+off) --game-index off`, so game 380 of a cell is the SAME GAME in every run, at every
# depth, on both arms. That is what makes a chunk replaceable -- and replaceable at chunk granularity
# is the whole point, because it is what lets a play change be absorbed by re-running the affected
# GAMES rather than the affected CELLS (26 cells / 10,400 games vs 24 chunks / ~590).
#
# The flat fields (games/lp_sum/batches/ms) are DERIVED and kept in sync for other readers
# (scripts/valueleaf.sh status, emit_table). chunks[] is the source of truth.
#
# It also fixes a bias the adaptive chunk size introduced: the table averaged BATCH MEANS, which is
# only correct while every batch is the same size. A 3-game chunk counted as much as a 25-game one.
# cell_mean() weights by games.
def _refresh(c):
    ch = c["chunks"]
    c["games"]   = sum(x["n"] for x in ch)
    c["batches"] = len(ch)
    c["ms"]      = sum(x["ms"] for x in ch)
    c["lp_mean"] = (sum(x["lp"] * x["n"] for x in ch) / c["games"]) if c["games"] else float("nan")
    # Compat shim: readers that predate chunks compute lp_sum/batches to get the mean.
    c["lp_sum"]  = c["lp_mean"] * c["batches"] if c["games"] else 0.0
    return c


def cell_mean(c): return c["lp_mean"]


def _covered(c):
    """Sorted [off,end) intervals this cell holds results for."""
    return sorted(((x["off"], x["off"] + x["n"]) for x in c["chunks"]))


def _missing(c, target):
    """The offsets below `target` with no result -- a list of [lo,hi). Normally one range at the end,
    but a resync can leave a hole in the middle, so this is computed generally rather than assumed."""
    out, pos = [], 0
    for lo, hi in _covered(c):
        if lo > pos: out.append((pos, min(lo, target)))
        pos = max(pos, hi)
        if pos >= target: break
    if pos < target: out.append((pos, target))
    return [(a, b) for a, b in out if b > a]


def _split_chunk(x, at):
    """Keep only [off,at) of chunk x. lp (a mean) carries over unchanged; ms is apportioned by count.

    Exact for a whole-chunk drop. For a legacy chunk -- one synthetic record standing in for a run
    that predates this schema -- the split is structurally exact (the old batches were uniform and
    offset-aligned) but the kept prefix inherits the WHOLE cell's mean instead of the surviving
    batches' mean. On a 400-game cell losing its last 25 that is an error of about
    |batch_mean - cell_mean| / 15 ~ 0.01 turns. Recorded here rather than hidden: every chunk written
    from now on is individually addressable, so it cannot recur."""
    n = at - x["off"]
    if n <= 0: return None
    x = dict(x); x["ms"] = x["ms"] * n / x["n"]; x["n"] = n
    return x


def _chunk_minus(x, bad):
    """The sub-chunks of x whose offsets are NOT in `bad`, as a list of maximal runs.

    The general form of _split_chunk (which only keeps a prefix): a cross-cell engine disagreement
    can land anywhere in a chunk, not just at its tail. lp (a mean) carries to each surviving piece
    and ms is apportioned by game count -- the same approximation _split_chunk documents."""
    if not bad: return [x]
    end, out, run = x["off"] + x["n"], [], None
    for o in range(x["off"], end + 1):
        inside = o < end and o not in bad
        if inside and run is None:
            run = o
        elif not inside and run is not None:
            n = o - run
            y = dict(x); y["off"] = run; y["n"] = n; y["ms"] = x["ms"] * n / x["n"]
            out.append(y); run = None
    return out


def _offset_src_conflicts(cells):
    """(deck,seed) -> the set of game offsets measured on DIFFERENT engines in different cells.

    A cell may legitimately be a MIX of engines (resync_engine_change deliberately keeps the prefix
    below B on its original engine). What may NOT vary is the engine for a GIVEN GAME across cells,
    because every way the table is read -- down a column (does depth d+1 beat depth d?) and across
    arms (V vs H at one depth) -- is a per-game comparison. If game 380 ran on engine A at V8 and on
    engine B at H5, that single difference is attributed to DEPTH when it is really a code change."""
    per_seed = {}
    for c in cells:
        by_off = per_seed.setdefault((c["deck"], c["seed"]), {})
        for x in c["chunks"]:
            for o in range(x["off"], x["off"] + x["n"]):
                by_off.setdefault(o, set()).add(x.get("src"))
    return {k: {o for o, s in v.items() if len(s) > 1} for k, v in per_seed.items()}


def enforce_offset_src_agreement(cells, log=print):
    """Drop every game whose engine DISAGREES across the cells of its (deck,seed), so it is re-run
    once, on one engine, everywhere it is needed.

    This is the invariant resync_engine_change intends but cannot maintain alone. That function picks
    a single split point B per seed from the state as it stands at ONE resume, and heals everything at
    or above it. It is correct for the change it sees -- but the src can change AGAIN before the games
    it just scheduled have all landed, and the second change is never inspected. That is exactly how
    the pre-merge FiveColour matrix ended up with 63 games (H5 s10010, H4/H5 s11011) on a third engine
    that no V cell ever ran: those three cells were the slowest, so they were the ones still unfinished
    when the source moved, and the contamination therefore landed ONLY on the deep heuristic cells it
    made look good. Checking per OFFSET rather than per prefix is what makes this idempotent: it re-
    derives the invariant from the state itself, so it holds no matter how many times the src moved."""
    conflicts = _offset_src_conflicts(cells)
    groups = {}
    for c in cells: groups.setdefault((c["deck"], c["seed"]), []).append(c)
    plan = []
    for key, bad in sorted(conflicts.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        if not bad: continue
        for c in groups[key]:
            keep, dropped = [], 0
            for x in sorted(c["chunks"], key=lambda x: x["off"]):
                pieces = _chunk_minus(x, bad)
                dropped += x["n"] - sum(p["n"] for p in pieces)
                keep.extend(pieces)
            if dropped:
                c["chunks"] = keep; _refresh(c)
                plan.append((key[0], key[1], "%s%d" % (c["arm"], c["depth"]), dropped))
        log("resync: s%d has %d game(s) measured on DIFFERENT engines across cells -- re-running them "
            "everywhere (offsets %s)" % (key[1], len(bad), _fmt_ranges(sorted(bad))))
    if plan:
        for deck, seed, cell, n in plan:
            log("  s%-6d %-4s drop %d mismatched game(s)" % (seed, cell, n))
    return plan


def _fmt_ranges(offs):
    """[375,376,...,393] -> '[375,394)' -- compact enough to log every conflicting offset."""
    out, i = [], 0
    while i < len(offs):
        j = i
        while j + 1 < len(offs) and offs[j + 1] == offs[j] + 1: j += 1
        out.append("[%d,%d)" % (offs[i], offs[j] + 1)); i = j + 1
    return " ".join(out)


def _src_fingerprint():
    """Engine identity = the tree hash of src/. Same value => same play, which is the property the
    freeze exists to protect. `git rev-parse HEAD:src` is what scripts/valueleaf.sh records too."""
    try:
        r = subprocess.run(["git", "rev-parse", "HEAD:src"], capture_output=True, text=True)
        return r.stdout.strip() or None
    except Exception:
        return None


def resync_engine_change(cells, target, src_now, log=print):
    """Absorb a play change WITHOUT throwing the run away: re-run the affected GAMES everywhere they
    were already generated, and keep everything below them.

    The rule (user, 2026-08-09): generation may continue under a play mismatch, but any game chunk
    that is not completely done must be regenerated -- "across all entries of the table that have
    generated them already, so some cells will not need to generate them".

    Why the chunk and not the seed. The table is read two ways: down a column (does depth d+1 beat
    depth d?) and across arms (V vs H at the same depth). Both are PER-GAME comparisons averaged over
    the seed. So what has to hold is that game 380 was measured on ONE engine at every depth and in
    both arms -- not that the whole seed was. Splitting at an offset gives exactly that: offsets
    [0,B) are engine A everywhere, [B,target) are engine B everywhere, and every column and every
    arm-difference is computed from games that agree. Re-running whole cells buys nothing extra and
    cost 140x more here (26 cells / 10,400 games against 24 chunks / ~590).

    The result is a SLIGHTLY MIXED matrix and that is deliberate (user, 2026-08-09): the prefix
    below B keeps its original engine in every cell, and only the games that were never finished
    are regenerated -- in each cell where they exist. Re-running whole cells to remove the mixing
    would cost 10,400 games against 587 for the same per-game agreement across cells.

    KEEP EVERY FULL SET, CONTIGUOUS OR NOT (user, 2026-08-12). What has to hold is per GAME, so the
    question for offset o is simply: does EVERY cell that still owes work already hold o? If yes, o was
    measured on the old engine everywhere it appears and no future game can mix it, so it stays --
    whether or not the offsets below it survived. If no, some cell will run o on the NEW engine, which
    would make o mixed across cells, so o goes everywhere.

    This used to be a CONTIGUOUS prefix B = min over incomplete cells of the run of offsets from 0,
    truncated at the first hole. That is sound but needlessly lossy, because cells do not finish in
    order: chunks within a level land at different times, so a perfectly good full set sitting above an
    in-flight hole was thrown away with it. With set-completion ordering (build_queue) holes are
    normal and short-lived, which makes the difference routine rather than exotic.

    Cells at target are excluded from the vote -- they owe nothing, so they cannot re-run anything and
    cannot mix an offset -- but they are still SUBJECT to the drop, because an offset an incomplete
    cell will redo must not survive in a complete one either. That also keeps a condemned cell capped
    at 50 untouched: the incomplete cells all hold offsets 0..50, so nothing of its is dropped.
    """
    if not src_now: log("resync: no src fingerprint (not a git tree?) -- skipping"); return []
    groups = {}
    for c in cells: groups.setdefault((c["deck"], c["seed"]), []).append(c)

    def covered(c):
        """The offsets this cell holds a result for, by whatever engine."""
        return {o for x in c["chunks"] for o in range(x["off"], x["off"] + x["n"])}

    plan = []
    for (deck, seed), cs in sorted(groups.items()):
        incomplete = [c for c in cs if c["games"] < target(c)]
        if not incomplete: continue                  # seed complete => internally consistent
        keepable = set.intersection(*(covered(c) for c in incomplete))
        for c in cs:
            keep, dropped = [], 0
            for x in sorted(c["chunks"], key=lambda x: x["off"]):
                if x.get("src") == src_now:          # already the current engine: nothing to absorb
                    keep.append(x); continue
                pieces = _chunk_minus(x, {o for o in range(x["off"], x["off"] + x["n"])
                                          if o not in keepable})
                dropped += x["n"] - sum(p["n"] for p in pieces)
                keep.extend(pieces)
            if dropped:
                c["chunks"] = keep; _refresh(c)
                plan.append((deck, seed, "%s%d" % (c["arm"], c["depth"]), dropped))
    if plan:
        tot = sum(p[3] for p in plan)
        log("resync: src changed since these results were written -- %d cells, %d games to redo "
            "(every offset held by all unfinished cells is kept)" % (len(plan), tot))
        for deck, seed, cell, n in plan:
            log("  s%-6d %-4s drop %d game(s) not in a full set" % (seed, cell, n))
    return plan


def emit_table(cells, args):
    """Parser-compatible legacy table (mean over seeds) + per-cell game counts, rewritten each batch."""
    L=["",
       "===== DEPTH MATRIX (UNBOUNDED, INCREMENTAL)  games=%d seeds=%s value_min_depth=%d  %s  hgames=%d =====" % (
         args.target, args.seeds, args.value_min_depth,
         "[PURE value-leaf, no redo]" if args.value_min_depth==0 else "[HYBRID redo]", args.target)]
    for dname in args.decks:
        dc=[c for c in cells if c["deck"]==dname]
        nseed=len(set(c["seed"] for c in dc if c["batches"]>0))
        L.append("---- %s (mean over %d seeds) ----" % (dname, nseed))
        def row(arm, depths, label):
            parts=[]; ann=[]
            for d in depths:
                cs=[c for c in dc if c["arm"]==arm and c["depth"]==d and c["batches"]>0]
                if not cs: continue
                lp=sum(cell_mean(c) for c in cs)/len(cs)
                ms=sum(c["ms"]/max(c["games"],1)*1000 for c in cs)/len(cs)
                g=min(c["games"] for c in cs)
                tag="*" if any(c["intractable"] for c in cs) else ""
                parts.append("%s%d=%.4f[%.1fms]%s"%(arm,d,lp,ms,tag)); ann.append("%s%d:%dg"%(arm,d,g))
            if parts:
                L.append("  %s %s"%(label,"   ".join(parts)))
                L.append("    # games/cell: %s   (*=intractable=reference-only)"%("  ".join(ann)))
        row("H", args.hdepths, "heuristic: ")
        row("V", args.vdepths, "value-leaf:")
        # READ-SIDE GUARD. enforce_offset_src_agreement heals this at every resume, so a table
        # written by a current run cannot carry a conflict -- but an OLD state file can, and the
        # table outlives the run that made it (the metadata deriver and every later reader consume
        # this file, not the state). A conflict makes a per-game comparison attribute a code change
        # to depth, so it is named in the artifact itself rather than left to be rediscovered.
        conf={k:v for k,v in _offset_src_conflicts(dc).items() if v}
        if conf:
            L.append("    !! ENGINE-MIXED: %d game(s) were measured on DIFFERENT engines across cells"
                     " -- per-game comparisons below are NOT apples-to-apples" %
                     sum(len(v) for v in conf.values()))
            for (dk,seed),bad in sorted(conf.items()):
                L.append("    !!   s%d: %s" % (seed, _fmt_ranges(sorted(bad))))
    open(args.out,"w").write("\n".join(L)+"\n")


def run_incremental(args):
    cells=[]
    for dname in args.decks:
        for seed in args.seeds:
            for d in args.hdepths: cells.append(dict(deck=dname,arm="H",depth=d,seed=seed))
            for d in args.vdepths: cells.append(dict(deck=dname,arm="V",depth=d,seed=seed))
    for c in cells:
        c.update(chunks=[], intractable=False, running=False, first_wall=None); _refresh(c)
    state_path=args.out+".cells.json"
    src_now=_src_fingerprint()
    if os.path.exists(state_path):                       # resume: skip already-committed chunks
        try:
            saved={_cell_key(x):x for x in json.load(open(state_path))}
            # A state file written before chunks[] existed carries only totals, so its games can only
            # be attributed to ONE engine -- the src the run was started under, recorded by
            # scripts/valueleaf.sh next to the state. That is right for every cell that was not
            # resumed across a src change; where it is wrong it is wrong CONSERVATIVELY (games the
            # new engine already produced get re-run, which costs time and cannot corrupt anything).
            legacy_src=None
            fs=os.path.join(os.path.dirname(os.path.abspath(state_path)),"freeze.src")
            if os.path.exists(fs): legacy_src=open(fs).read().strip() or None
            for c in cells:
                s=saved.get(_cell_key(c))
                if not s: continue
                c["intractable"]=s["intractable"]; c["first_wall"]=s.get("first_wall")
                if s.get("chunks"):
                    c["chunks"]=s["chunks"]
                elif s.get("games"):
                    c["chunks"]=[{"off":0,"n":s["games"],
                                  "lp":s["lp_sum"]/max(s["batches"],1),"ms":s["ms"],
                                  "src":legacy_src,"legacy":s["batches"]}]
                _refresh(c)
        except Exception: pass
        print("resumed %d cells from %s" % (sum(1 for c in cells if c["batches"]), state_path))

    lock=threading.Lock()
    for c in cells: c["inflight_games"]=0
    def target(c): return min(args.reference_target,args.target) if c["intractable"] else args.target

    # A play change since these results were written is absorbed here, at chunk granularity: the
    # unfinished games are regenerated in each cell where they exist, and the prefix below them keeps
    # its original engine. That is what makes landing an optimization mid-table affordable.
    resync_engine_change(cells, target, src_now)

    # ... and the invariant that absorption is FOR is re-derived from the state itself, every resume.
    # resync_engine_change heals one src change at the split point it can see; this catches a change
    # that landed while its games were still in flight, which is how three deep-H cells (and no V
    # cell) ended up carrying 63 games of a third engine. Idempotent and cheap: with no conflict it
    # walks the offsets once and drops nothing.
    enforce_offset_src_agreement(cells)

    # ---------------------------------------------------------------- PROPER BATCHING: one work QUEUE
    # ONE queue of fixed-size chunks, handed to `workers` threads that are kept full until the queue
    # drains. Not one chunk per cell, not a chunk size that shrinks: a QUEUE.
    #
    # What this replaces and why. The old scheduler picked the cell with the FEWEST games (breadth
    # first) and allowed ceil(workers/open cells) chunks of it in flight. Two failures follow from
    # that, and both were measured on this deck:
    #   * breadth-first is exactly backwards. It starts the CHEAP work first, so the expensive cells
    #     are the ones still running when everything else has drained -- 3 of 24 cores for fifteen
    #     hours, with two H5 chunks at 54,004s elapsed.
    #   * parallelism was capped by the number of OPEN CELLS, so it collapsed as the run finished.
    #
    # LONGEST-PROCESSING-TIME FIRST fixes the first: start the slowest chunks at the beginning, when
    # there is still cheap work to fill the other cores, and they finish alongside it instead of
    # after it. A single queue fixes the second -- a free worker takes the next chunk from anywhere.
    # The residual tail is then one chunk, which is the honest floor for subprocess-level batching
    # (game-level pooling inside ONE `mtg --batch` needs a manifest game_index field: src, next).
    def est_spg(c):
        # Measured rate when we have one (a resume has 49 of 52), else a depth model: the H arm grows
        # about 4x per level (measured 0.5 / 5.9 / 36 / 161 / 325 s at H1..H5) and the V arm about 3x
        # from a far lower base (0.009 .. 9.4 s at V1..V8). Only the ORDER matters to LPT, so a rough
        # model is fine -- and it is only ever used for a cell with no games yet.
        if c["games"]: return c["ms"]/c["games"]
        return (4.0**c["depth"]) if c["arm"]=="H" else (3.0**c["depth"])*0.02

    def build_queue():
        # Every missing offset up to the cell's target. There is no wave cap any more: the pool
        # admits condemnable cells a game at a time and judges them as they run, so nothing has to
        # be held back behind a barrier.
        # Chunk size still matters even though the pool no longer runs one process per chunk -- it is
        # the unit of DURABILITY (a chunk's result is written the moment it lands) and of resume, so
        # a wave with few games uses smaller chunks rather than leaving most of them un-committed
        # until the end.
        # The work is the set of MISSING OFFSETS, not "target minus a count". After a resync those are
        # not always a suffix -- a cell can be left with a hole in the middle -- so the gaps are
        # computed from the chunks the cell actually holds.
        gaps={id(c):_missing(c,target(c)) for c in cells}
        total=sum(hi-lo for g in gaps.values() for lo,hi in g)
        chunk=max(1,min(args.batch, total//max(1,args.workers))) if total else args.batch
        q=[]
        for c in cells:
            for lo,hi in gaps[id(c)]:
                off=lo
                while off<hi:
                    # GRID-ALIGNED: a chunk never straddles a multiple of args.batch, so the same
                    # offsets form the same chunk in every cell and a chunk is a comparable unit
                    # across arms/depths. Without this the boundaries follow each cell's own gap
                    # start, so a resynced cell chunks [375,394) while its neighbours hold one
                    # [0,394) -- and a per-chunk engine check has nothing to compare.
                    n=min(chunk, args.batch-off%args.batch, hi-off)
                    q.append({"c":c,"off":off,"n":n})
                    off+=n
        # LEVEL-ORDER THE FIRST 3/4, PACK THE LAST (user, 2026-08-12). LPT and the resync's banking
        # rule pull in opposite directions, and unmediated the banking rule loses: resync_engine_change
        # keeps only offsets below B = min, over the seed's INCOMPLETE cells, of their contiguous
        # prefix -- so ONE cell with zero games forces B=0 and an engine change discards the whole
        # seed. LPT guarantees exactly that state for hours (it runs the costliest chunks first, so the
        # cheap cells have not started at all): measured 2026-08-11, a run holding 4367 games across
        # 7.75 h would have banked NONE of them. A Windows Update then killed the next run outright,
        # which is the same loss arriving by a different route.
        #
        # So advance every cell together through the first 3/4 of its target -- offset level by offset
        # level, all cells at level k before level k+1 -- which keeps B climbing with the run and makes
        # a stop at ANY convenient point cost only the level in flight. Then switch to LPT for the last
        # quarter, where packing is what protects the makespan: the residual tail is one chunk of the
        # most expensive cell, and that is the honest floor for chunk-level batching.
        #
        # CONDEMNED cells keep plain LPT and stay at the front (user, 2026-08-12). They are capped at
        # the reference target rather than run to 400, their whole purpose is to be judged early so the
        # queue is freed, and level-ordering them would delay that verdict for no banking gain.
        #
        # The test is whether a cell IS condemned, never whether its depth makes it condemnABLE. Keying
        # on depth > never-condemn was wrong twice over: V6/V7/V8 run at ~15-17 s/game, nowhere near the
        # 60 s/game cutoff, so they are never condemned and are FULL 400-game rows of the table -- yet
        # the depth test filed them as reference-only and handed 2500 games of them absolute priority.
        # Observed 2026-08-12: every thread sat on that work while H1-H5 and V1-V5 stayed at zero. Only
        # 7 of 56 cells are actually condemned here (H6 at every seed, V6/V7/V8 at s8008 alone), which
        # is the handful this zone is for -- a few of them alongside a whole lot of everything else,
        # not the pool to themselves. An unjudged cell is level-ordered like any other and simply stops
        # being queued if the guard later condemns it.
        #
        # Ordering WITHIN a level stays cost-descending: B is gated by the SLOWEST cell's chunk landing,
        # so starting the expensive chunk of each level first minimises time-to-protection. Cheapest-
        # first would bank trivia instantly but push the binding cell later, moving B off zero LATER.
        # The EXPENSIVE CELL IS NOT SPECIAL-CASED (user, 2026-08-12). H5 leads, exactly as it does under
        # plain LPT -- it is simply that a SET is completed before the next one starts. Cost-descending
        # is the within-level tiebreak, so H5 is the first chunk of every level and the first chunk of
        # the packed quarter; it is never held back. The single thing that changes versus LPT is the
        # primary key: finish the set, then move on.
        # ONE chunk of a condemned cell at a time, but PRIORITISED (user, 2026-08-12). Note what this
        # does and does NOT control: OCCUPANCY of the pool by depth>never-condemn cells is the ENGINE's
        # to decide, not this weight's. BatchRunner pulls every condemnable cell's games OUT of the
        # static list into per-cell queues and meters them in at `drip` games each (see its WORK POOL
        # comment), so a manifest weight only chooses the ORDER those cells seed the metered deque.
        # Keeping one chunk per condemned cell at the head keeps that order sensible -- it does not,
        # and cannot, stop them taking threads. See docs/design/batch-drip-release.md for the part
        # that can.
        first_out = {}
        for ch in sorted(q, key=lambda ch: ch["off"]):
            first_out.setdefault(id(ch["c"]), ch["off"])
        def zone(ch):
            c = ch["c"]
            if c["intractable"] and ch["off"] == first_out[id(c)]:
                return 0                                          # CONDEMNED, next chunk: up front
            return 1 if ch["off"] < 0.75*target(c) else 2         # level-order, then packed
        q.sort(key=lambda ch: (zone(ch),
                               ch["off"] if zone(ch) == 1 else 0,
                               -est_spg(ch["c"])*ch["n"]))
        # ...and CARRY that order into the pool, which does its own sort. Every chunk becomes a job in
        # ONE `mtg --batch`, and BatchRunner re-sorts jobs by the manifest's `weight` DESCENDING (see
        # BatchRunner.cpp, sched_weight). A plain cost weight therefore reimposed pure LPT and made this
        # ordering INERT -- observed 2026-08-12: three minutes in, H1-H4 and V1-V5 still had zero games
        # because the pool was running the costliest chunks first regardless of queue position.
        #
        # So encode the whole key in the weight. sched_weight is a 32-bit int, so the fields are packed
        # to stay well inside it: zone (0..2) at 1e8, level at 1e4, cost at 1e0.
        #   zone   : (2-z)*1e8      -- 0 (condemned) outranks 1 (level-order) outranks 2 (packed)
        #   level  : (target-off)*1e4 for the level-order zone only, so a LOWER offset outranks a
        #            higher one; zero elsewhere, which leaves those zones ordered purely by cost (LPT)
        #   cost   : int(s/game*10), capped below 1e4 so it can only ever break ties within a level
        # Max is ~2.0e8, comfortably inside int32.
        for ch in q:
            z = zone(ch); c = ch["c"]
            lvl = (target(c) - ch["off"])*10_000 if z == 1 else 0
            ch["w"] = (2-z)*100_000_000 + lvl + min(int(est_spg(c)*10), 9_999)
        return q

    def write_state():
        tmp=state_path+".tmp"
        json.dump([{k:c[k] for k in ("deck","arm","depth","seed","games","lp_sum","lp_mean","batches",
                                     "ms","intractable","first_wall","chunks")} for c in cells], open(tmp,"w"))
        os.replace(tmp,state_path)
        emit_table(cells,args)

    # ------------------------------------------------------- ONE PROCESS, ONE POOL, ONE TAIL
    # The WHOLE matrix -- every cell, both arms, straight to target -- goes into a single
    # `mtg --batch`, which flattens every game of every chunk into one work queue. No waves and no
    # per-arm split, because neither is a real dependency any more:
    #
    #   * The ARM used to be process environment (MTG_VALUE_MODEL and friends, each read once into a
    #     process-wide static), so one process could only BE one arm. It is now a per-job manifest
    #     field, so H and V cells share the queue and the cheap V arm backfills cores while the
    #     expensive H arm drains. See ai/ValueArm.h.
    #   * CONDEMNATION used to need a barrier: run everything to a reference floor, stop the world,
    #     judge. It is now applied inside the pool -- on the cell's running mean (in-flight games
    #     included), on a single-game limit checked while the game is still running, with the cell's
    #     remaining games skipped at dequeue. See BatchRunner's CondemnRule.
    #   * Condemnable cells are METERED rather than sorted: LPT would otherwise put every thread on
    #     the most expensive cell in the table, which is precisely the one most likely to be thrown
    #     away. The pool admits `drip` of each at a time and refills on each uncondemned completion,
    #     while the protected cells keep the rest of the box busy.
    #
    # What this replaces: one subprocess per chunk made the chunk size the tail granularity (measured
    # 2026-08-08, two 25-game H5 chunks still running at 54,004 s with 3 of 24 cores busy for fifteen
    # hours), and the per-(deck,arm)-per-wave pools that followed still idled ~15 of 20 threads for
    # hours (2026-08-10). One queue has one tail, and it is one GAME long.
    def run_pool(chunks):
        by_name={}
        jobs=[]
        for ch in chunks:
                c=ch["c"]
                dname, arm = c["deck"], c["arm"]
                deck_file, prof, mt = DECKS[dname]
                nm="%s%d_s%d_off%d" % (c["arm"], c["depth"], c["seed"], ch["off"])
                by_name[nm]=ch
                job={"name":nm, "deck":deck_file, "profile":PROFILES.get(dname),
                     "games":ch["n"],
                     # The chunk's shuffle seeds are base+off .. base+off+n, and its GLOBAL
                     # game numbers are off .. off+n -- exactly the single-run
                     # `--seed (base+off) --game-index off` form this used to spawn.
                     "seed":c["seed"]+ch["off"], "game_index":ch["off"],
                     "depth":c["depth"], "budget_ms":0,   # 0 = unbounded (the H arm's point)
                     "max_turns":mt, "ignore_play_profile":True,
                     # Every chunk of a cell shares one `cell`, which is what condemnation groups on:
                     # the guard judges the CELL's cumulative rate, never one chunk's (a single
                     # unlucky 25-game chunk once condemned a cell averaging 5.5 s/game against a
                     # 60 s/game limit -- dragonstorm H5 s11011, 2026-08-04).
                     "cell":"%s_%s%d_s%d" % (dname, c["arm"], c["depth"], c["seed"]),
                     # Priority for the pool's own sort (BatchRunner sorts by this DESCENDING). Packed
                     # in build_queue so the queue's order survives into the pool; a bare cost weight
                     # here silently reimposed LPT and made the set-completion ordering inert.
                     "weight":ch["w"]}
                # THE ARM, per job (see ai/ValueArm.h). This is what lets both arms share one queue.
                if c["arm"]=="V":
                    job.update({"value_model":True, "value_profile":prof,
                                "value_min_depth":args.value_min_depth,
                                "value_startgate_alpha":8.0})
                else:
                    # The H-cell ladder runs its warm-up passes on the cheap leaf and only the
                    # committed pass on the heuristic. Guarded on the sidecar EXISTING, so a missing
                    # model does not error -- every H cell just silently takes the slow path
                    # (1.35x-84.8x more work).
                    job["value_model"]=False
                    if prof and os.path.exists(prof):
                        job["value_profile"]=prof; job["ladder_value_leaf"]=True
                jobs.append(job)
        man=args.out+".manifest.json"
        # CONDEMNATION now travels with the manifest and is applied inside the pool, continuously.
        condemn={"sec_per_game":args.intractable_sec_per_game,
                 "reference_games":args.reference_target,
                 "never_condemn_depth":args.never_condemn_at_or_below,
                 "max_game_sec":args.max_game_sec,
                 "drip":args.drip}
        json.dump({"condemn":condemn, "jobs":jobs}, open(man,"w"))
        env=dict(os.environ)
        # The arm is on the JOB now, so a stray arm variable in the caller's environment would apply
        # to every job that does not override it -- silently measuring one arm twice. Strip them.
        for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
                  "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE",
                  "MTG_VALUE_STARTGATE_ALPHA","MTG_LADDER_VALUE_LEAF"):
            env.pop(k, None)
        env.setdefault("MTG_BATCH_HEARTBEAT", os.path.join(os.path.dirname(args.out) or ".",
                                                           "heartbeat.txt"))
        # MEMORY BOUND, not a tuning knob. The matrix runs UNBOUNDED search on every worker at once,
        # and the transposition table is an unbounded memoization of SimulateToEnd -- so the pool's
        # footprint grows without limit. Measured 2026-08-11 on FiveColour: one `mtg --batch` reached
        # 43.9 GB RES / 93.3% of a 47 GB box with 0 GB available, i.e. one bad game away from the OOM
        # killer taking a 13-hour run with it.
        #
        # The cap is RESULT-NEUTRAL by construction (see TranspositionTable::Cap): the table is a pure
        # memoization, so refusing to store past the cap trades recompute for bounded memory and every
        # decision is unchanged. Early shallow high-reuse leaves are stored first and kept; only the
        # deep long tail is dropped. An entry is ~64 B (16 B key + 4 B value + node/bucket overhead),
        # so 8M entries is ~0.5 GB per live table -- roughly 12-24 GB across 24 workers, which leaves
        # real headroom on this box instead of running to the ceiling.
        env.setdefault("MTG_TT_CAP", "8000000")
        games=sum(j["games"] for j in jobs)
        nH=sum(1 for j in jobs if not j.get("value_model"))
        print("ONE pool: %d chunks (%d H, %d V), %d games, %d threads; condemn %s"
              % (len(jobs), nH, len(jobs)-nH, games, args.workers, condemn), flush=True)
        print("  heartbeat -> %s (slowest 100 games, running ones included)"
              % env["MTG_BATCH_HEARTBEAT"], flush=True)
        # --threads only when explicitly asked for; otherwise let the engine resolve to every
        # available core (see --workers). Passing a number always would re-introduce the very
        # hand-picked cap this run exists to avoid.
        cmd=[MTG,"--batch",man]+(["--threads",str(args.workers)] if args.workers>0 else [])
        # stderr carries the engine's SLOW-GAME and CONDEMNED lines (each SLOW-GAME a
        # self-contained repro, tagged job=<cell>_off<offset>). Drained on its own thread so a full
        # pipe can never block the pool, and handled as it arrives rather than collected at exit --
        # on this deck a single game ran for 21 hours, and a report you only get afterwards is not a report.
        pr=subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            text=True, env=env)
        slow_path=os.environ.get("MTG_SLOW_GAME_LOG","logs/eval/slow_games.log")
        def drain_slow(stream, tag):
            try:
                for l in stream:
                    # CONDEMNED comes back on the same stream. Mirror it into the cell state so the
                    # table, the resume logic and the pool agree on what was actually judged --
                    # otherwise a resumed run would keep re-queueing a cell the pool has given up on.
                    if "CONDEMNED" in l:
                        print(l.rstrip(), flush=True)
                        m=re.search(r"cell=(\S+)", l)
                        if m:
                            for c in cells:
                                if "%s_%s%d_s%d" % (c["deck"],c["arm"],c["depth"],c["seed"]) == m.group(1):
                                    c["intractable"]=True
                        continue
                    if "SLOW-GAME" not in l: continue
                    with _slow_lock:
                        with open(slow_path,"a") as fh: fh.write("%s %s\n" % (tag, l.strip()))
            except Exception: pass
        # CONDEMNED lines come back on the same stream; mirror them into the cell state so the table
        # and the resume logic agree with what the pool actually did.
        st=threading.Thread(target=drain_slow, args=(pr.stderr, "pool"), daemon=True)
        st.start()
        done=0
        # Commit each chunk THE INSTANT its line arrives. The pool streams a job's result as
        # soon as that job's last game lands, so durability is per chunk exactly as before --
        # killing the run mid-wave loses only the chunks still in flight.
        for line in pr.stdout:
            m=re.match(r"(\S+): played=(\d+) avg=([\d.]+) digest=(\w+) ms=(\d+)", line.strip())
            if not m: continue
            nm,p,lp,_dg,ms = m.group(1), int(m.group(2)), float(m.group(3)), m.group(4), int(m.group(5))
            ch=by_name.get(nm)
            if ch is None or p<=0: continue
            c=ch["c"]; wall=ms/1000.0
            c["chunks"].append({"off":ch["off"],"n":p,"lp":lp,"ms":wall,"src":src_now})
            _refresh(c)
            if c["first_wall"] is None: c["first_wall"]=wall
            done+=1
            write_state()
            if done % 10 == 0:
                tot=sum(x["games"] for x in cells)
                print("... %d chunks, %d games total" % (done, tot), flush=True)
        pr.wait()
        st.join(timeout=5)

    # ONE pass. No floor wave: the pool judges tractability as it runs (running mean including
    # in-flight games, plus a single-game limit checked while the game is still running) and skips a
    # condemned cell's remaining games at dequeue, so there is nothing for a barrier to synchronise.
    q=build_queue()
    total_chunks=len(q)
    if q:
        print("queue: %d chunks of <=%d games to target, slowest first: %s"
              % (len(q), args.batch,
                 "%s%d s%s" % (q[0]["c"]["arm"], q[0]["c"]["depth"], q[0]["c"]["seed"])), flush=True)
        run_pool(q)
        write_state()

    done_batches=total_chunks
    print("=== incremental generation complete: %d batches, %d cells (%d intractable) ===" % (
        done_batches, len(cells), sum(1 for c in cells if c["intractable"])))


def main():
    ap=argparse.ArgumentParser()
    ap.add_argument("--games",type=int,default=1000)
    ap.add_argument("--hgames",type=int,default=None,
                    help="games for the EXPENSIVE heuristic arm (combo decks: H4/H5 cost seconds/game). Default "
                         "= --games. Cost-sample it; the derivation's monotone-H envelope guards a noisy deep H.")
    ap.add_argument("--vgames",type=int,default=None,
                    help="games for the CHEAP value-leaf arm (O(1) leaf). Default = --games. Keep this full: the "
                         "value-leaf-by-depth is the useful, decision-setting side of the table.")
    ap.add_argument("--hgames-depth",nargs="+",default=[],metavar="D:G",
                    help="per-heuristic-depth games override for the SLOW cells only (e.g. 3:500 4:200 5:100 on a "
                         "combo deck). Depths not listed use --hgames/--games. Cut games ONLY where the heuristic "
                         "is genuinely expensive -- cheap decks (slivers/knights/burn/TH) keep FULL games at every "
                         "depth incl. H5; do not pass this for them.")
    ap.add_argument("--seeds",nargs="+",type=int,default=[4004,5005,6006,7007])
    ap.add_argument("--hdepths",nargs="*",type=int,default=[1,2,3,4,5])
    ap.add_argument("--vdepths",nargs="*",type=int,default=[1,2,3,4,5])
    ap.add_argument("--value-min-depth",type=int,default=0,
                    help="MTG_VALUE_MIN_DEPTH for the value arm; 0 = PURE value-leaf (no redo). Default 0.")
    ap.add_argument("--threads",type=int,default=6)
    ap.add_argument("--decks",nargs="+",default=list(DECKS))
    ap.add_argument("--out",default="logs/eval/valueleaf_depth_matrix.txt")
    ap.add_argument("--write-profile",action="store_true",
                    help="After measuring, AUTOMATICALLY fold this run's table into each deck's play profile "
                         "(<deck>.value.json) via valueleaf_table_to_metadata.py -- so measure+write is ONE "
                         "atomic command and the profile can never drift from a stale/hand-stitched log. Uses "
                         "ONLY this run's freshly-measured blocks (not the appended --out history). The writer's "
                         "completeness guard REFUSES a truncated ladder (extend --vdepths and re-run).")
    ap.add_argument("--allow-partial",action="store_true",
                    help="pass through to the profile writer: write even a truncated/inconclusive table "
                         "(--write-profile only). Use only when an UNSET trust depth is intentional.")
    # --- incremental / tractability-aware mode ---
    ap.add_argument("--incremental",action="store_true",default=True,
                    help="THE ONLY ROUTE, and now the default -- accepted for compatibility and ignored. "
                         "Round-robin BATCHES across every cell (breadth-first), written incrementally, "
                         "resumable, with cross-cell work-stealing and concurrent batches per cell. The "
                         "monolithic path was removed: it paid a load-imbalance tail PER CELL and could not "
                         "resume, which is how a run ended up at 3 of 24 cores for hours.")
    ap.add_argument("--no-incremental",dest="incremental",action="store_false",
                    help=argparse.SUPPRESS)
    ap.add_argument("--batch",type=int,default=50,help="games per incremental batch (default 50)")
    ap.add_argument("--target",type=int,default=None,help="full-sample target games/cell (default = --games)")
    ap.add_argument("--reference-target",type=int,default=100,
                    help="games/cell to stop at once a cell is marked INTRACTABLE (a slow, not-production-usable "
                         "cell still gets a reference value; default 100)")
    ap.add_argument("--intractable-sec-per-game",type=float,default=2.0,
                    help="a batch slower than this (wall sec / game, single-threaded) marks its cell intractable "
                         "-> capped at --reference-target. Checked on every batch (first batch is the main signal).")
    ap.add_argument("--never-condemn-at-or-below",type=int,default=5,
                    help="cells at or below this DEPTH are never marked intractable, however slow they measure. "
                         "These are the cells the trust-depth decision reads, so condemning one leaves a hole in "
                         "the answer rather than saving cost -- and since the check is on WALL time it can fire "
                         "purely because the box was oversubscribed. Set to 5 to protect the d<=5 ladder.")
    ap.add_argument("--max-game-sec",type=float,default=3600.0,
                    help="condemn a cell as soon as ONE of its games has been RUNNING this long -- checked on "
                         "in-flight games, so it does not wait for the game to end. Deliberately separate from "
                         "--intractable-sec-per-game: that one is a MEAN (is filling this cell affordable?), this "
                         "one is about a single pathological game, and sharing the number would condemn a cell "
                         "with a 5 s/game mean over one 61 s game. 0 disables. Default 3600 (a run measured "
                         "2026-08-10 had a single game at 21.4 HOURS, invisible until it finished).")
    ap.add_argument("--drip",type=int,default=1,
                    help="games of a single NOT-YET-JUDGED condemnable cell allowed in flight at once. LPT would "
                         "otherwise put every thread on the most expensive cell in the table -- which is exactly "
                         "the one most likely to be discarded. The pool admits this many and refills on each "
                         "uncondemned completion, while the protected (d<=never-condemn) cells, which must run in "
                         "full regardless, keep the rest of the box busy. Default 1.")
    ap.add_argument("--workers",type=int,default=0,
                    help="threads for the ONE pooled batch. DEFAULT 0 = let the engine resolve it, which "
                         "means AffinityCpuCount() -- every core the process is actually allowed to use. "
                         "That is strictly better than a number chosen here: os.cpu_count() over-reports "
                         "inside a cgroup with a restricted cpuset, and any hand-picked value is one more "
                         "way to under-fill the box. Pass a number only to deliberately run small.")
    args=ap.parse_args()
    if not args.incremental:
        sys.exit("the monolithic path is REMOVED -- incremental batching is the only route "
                 "(one pooled queue, resumable, no per-cell tail). Drop --no-incremental.")
    if args.never_condemn_at_or_below < 5:
        sys.exit("--never-condemn-at-or-below must be >= 5: the d<=5 H cells ARE the crossover, so "
                 "condemning one leaves a hole in the answer rather than saving cost.")
    if args.incremental:
        args.target = args.target if args.target is not None else args.games
        os.makedirs(os.path.dirname(args.out),exist_ok=True)
        run_incremental(args)
        return
    # NOTE: the monolithic per-cell path that used to live here is DELETED, not disabled. It called
    # run() once per (deck, depth, seed) -- a loop of ~50 separate `mtg` invocations, each with its own
    # load-imbalance tail and a serial gap between them. That is the shape CLAUDE.md forbids, and it is
    # what the 2026-08-10 post-mortem traced 23 hours at 3-of-20 cores back to. The guard above rejects
    # --no-incremental, but dead code that still reads as a working alternative is how a superseded
    # baseline gets re-adopted (see fivecolour-search-cost.md, where exactly that produced a retracted
    # 7x claim). run_incremental() -> run_pool() is the ONLY route: ONE manifest, ONE queue, ONE tail.
    if args.write_profile:
        # Fold THIS run's table straight into the play profile(s) -- one atomic command, no hand-stitched log.
        # Write this run's blocks to a fresh run-scoped log (NOT the appended --out) so the writer sees only
        # the ladder we just measured, then delegate to the completeness-guarded metadata writer.
        thisrun = args.out + ".thisrun.txt"
        with open(thisrun, "w") as f:
            f.write("\n".join(run_lines) + "\n")
        here = os.path.dirname(os.path.abspath(__file__))
        cmd = [sys.executable, os.path.join(here, "valueleaf_table_to_metadata.py"),
               thisrun, "--decks", *args.decks]
        if args.allow_partial:
            cmd.append("--allow-partial")
        print("\n=== --write-profile: folding this run's table into the play profile(s) ===", flush=True)
        rc = subprocess.run(cmd).returncode
        if rc != 0:
            print("!! profile write REFUSED (rc=%d): the ladder is incomplete. Extend --vdepths/--hdepths "
                  "to cover the deck's play target_depth and re-run, or pass --allow-partial." % rc)
            sys.exit(rc)


if __name__ == "__main__":
    main()
