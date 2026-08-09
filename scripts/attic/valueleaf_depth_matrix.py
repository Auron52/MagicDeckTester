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


def run(deck, depth, games, seed, mt, threads, profile, value_on, value_min_depth,
        deck_profile=None):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL","MTG_EVAL_PROFILE","MTG_VALUE_MODEL","MTG_VALUE_PROFILE",
              "MTG_NC_SEARCH","MTG_VALUE_MIN_DEPTH","MTG_VALUE_REDO_MODE","MTG_VALUE_STARTGATE_ALPHA"):
        env.pop(k, None)
    if value_on:
        env["MTG_VALUE_MODEL"]="1"; env["MTG_VALUE_PROFILE"]=profile
        # MIN_DEPTH=0 => PURE value-leaf (no heuristic redo); the whole point of this matrix.
        env["MTG_VALUE_MIN_DEPTH"]=str(value_min_depth); env["MTG_VALUE_STARTGATE_ALPHA"]="8"
    else:
        env["MTG_VALUE_MODEL"]="0"
    # --ignore-play-profile: a deck with an ENABLED value_play block (antilife/hinata since the beam
    # adoption) locks play depth and REJECTS an explicit --depth; this bypasses that so the matrix can
    # sweep depths. Harmless for decks without an enabled block (CLI depth just wins). REQUIRED now.
    cmd=[MTG,deck,"--games",str(games),"--seed",str(seed),"--depth",str(depth),
         "--max-turns",str(mt),"--threads",str(threads),"--ignore-play-profile"]
    if deck_profile: cmd += ["--profile", deck_profile]
    t0=time.time(); out=subprocess.run(cmd,capture_output=True,text=True,env=env).stdout; dt=time.time()-t0
    p=int(re.search(r"Games played\s*:\s*(\d+)",out).group(1))
    # Merged metric (a4f2be7): "avg (turns)" IS the loss-penalised avg win turn (unwon = max_turns+1)
    # printed directly -- identical to the old (w*a+(p-w)*(mt+1))/p, so numbers stay comparable to the
    # existing tables. (Was: parse "Games won" + "Avg win turn" and recompute; both lines are now gone.)
    m=re.search(r"avg \(turns\)\s*:\s*([\d.]+)",out); lp=float(m.group(1)) if m else float("nan")
    return lp, 1000.0*dt/p


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

    B is per (deck, seed), and is the lowest offset any cell still owes:
      * a seed whose cells are ALL at target is already consistent -- untouched, whatever its engine.
      * cells that never reached B keep everything (a condemned cell capped at 50 has nothing at or
        above B=375, so it is not touched and not extended).
      * a cell that has results at or above B from the OLD engine drops exactly those and re-runs
        them; results at or above B already produced by the CURRENT engine are kept.
    """
    if not src_now: log("resync: no src fingerprint (not a git tree?) -- skipping"); return []
    groups = {}
    for c in cells: groups.setdefault((c["deck"], c["seed"]), []).append(c)

    def foreign_end(c):
        """End of the contiguous prefix produced by an engine OTHER than the current one."""
        pos = 0
        for x in sorted(c["chunks"], key=lambda x: x["off"]):
            if x["off"] > pos: break                 # hole: prefix stops here
            if x.get("src") == src_now: break        # current engine: prefix stops here
            pos = max(pos, x["off"] + x["n"])
        return pos

    plan = []
    for (deck, seed), cs in sorted(groups.items()):
        incomplete = [c for c in cs if c["games"] < target(c)]
        if not incomplete: continue                  # seed complete => internally consistent
        B = min(foreign_end(c) for c in incomplete)
        for c in cs:
            keep, dropped = [], 0
            for x in sorted(c["chunks"], key=lambda x: x["off"]):
                if x.get("src") == src_now or x["off"] + x["n"] <= B:
                    keep.append(x); continue
                head = _split_chunk(x, B)            # None unless the chunk straddles B
                if head: keep.append(head)
                dropped += x["n"] - (head["n"] if head else 0)
            if dropped:
                c["chunks"] = keep; _refresh(c)
                plan.append((deck, seed, "%s%d" % (c["arm"], c["depth"]), B, dropped))
    if plan:
        tot = sum(p[4] for p in plan)
        log("resync: src changed since these results were written -- %d cells, %d games to redo"
            % (len(plan), tot))
        for deck, seed, cell, B, n in plan:
            log("  s%-6d %-4s drop [%d,+%d)" % (seed, cell, B, n))
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

    # A play change since these results were written is absorbed here, at chunk granularity.
    resync_engine_change(cells, target, src_now)

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
        # Chunk size is about the QUEUE having at least one piece per worker, not about shrinking as
        # the run drains. A full run (20,800 games) gets 832 chunks of 25 and never notices; a targeted
        # fill of 75 games would get THREE chunks of 25 -- three busy cores out of twenty -- so the
        # size drops to keep every worker fed. Computed once, up front, from the total outstanding.
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
                    n=min(chunk,hi-off)
                    q.append({"c":c,"off":off,"n":n})
                    off+=n
        q.sort(key=lambda ch: est_spg(ch["c"])*ch["n"], reverse=True)
        return q

    queue=build_queue()
    print("queue: %d chunks of <=%d games, longest-first (slowest cell first: %s)"
          % (len(queue), args.batch,
             ("%s%d s%s" % (queue[0]["c"]["arm"],queue[0]["c"]["depth"],queue[0]["c"]["seed"])) if queue else "-"),
          flush=True)

    def drop_condemned(c):
        # A condemned cell keeps only what it needs to reach reference_target; the rest leaves the
        # queue immediately so no worker ever picks it up.
        lim=target(c)
        queue[:] = [ch for ch in queue if not (ch["c"] is c and ch["off"] >= lim)]

    def write_state():
        tmp=state_path+".tmp"
        json.dump([{k:c[k] for k in ("deck","arm","depth","seed","games","lp_sum","lp_mean","batches",
                                     "ms","intractable","first_wall","chunks")} for c in cells], open(tmp,"w"))
        os.replace(tmp,state_path)
        emit_table(cells,args)

    ex=ThreadPoolExecutor(max_workers=args.workers)
    futs={}
    def submit_next():
        while queue:
            ch=queue.pop(0)
            c=ch["c"]
            if ch["off"]>=target(c): continue      # condemned after this chunk was queued
            c["inflight_games"]+=ch["n"]
            deck_file,prof,mt=DECKS[c["deck"]]
            futs[ex.submit(run_batch,deck_file,mt,c["depth"],c["seed"],ch["off"],ch["n"],
                           c["arm"]=="V",args.value_min_depth,prof,
                           PROFILES.get(c["deck"]))]=(c,ch)
            return True
        return False

    while len(futs)<args.workers and submit_next(): pass
    done_batches=0
    while futs:
        done,_=wait(list(futs),return_when=FIRST_COMPLETED)
        for fut in done:
            c,ch=futs.pop(fut)
            try: lp,wall,p=fut.result()
            except Exception: lp,wall,p=float("nan"),0.0,0
            with lock:
                c["inflight_games"]-=ch["n"]
                if p>0 and lp==lp:
                    # Recorded as a CHUNK, addressed by offset and stamped with the engine that
                    # produced it -- so this exact span of games can be replaced later without
                    # touching the rest of the cell.
                    c["chunks"].append({"off":ch["off"],"n":p,"lp":lp,"ms":wall,"src":src_now})
                    _refresh(c)
                    if c["first_wall"] is None: c["first_wall"]=wall
                    # Tractability keys on the cell's CUMULATIVE rate, not this batch's. Per-batch was
                    # wrong twice over: (1) a single unlucky 25-game batch holding one pathological
                    # game condemned the whole cell -- measured 2026-08-04, dragonstorm H5 seed 11011
                    # was flagged while averaging 5.5 s/game against a 60 s/game threshold, 11x under;
                    # and (2) the flag was STICKY, never re-examined as the average recovered, so one
                    # bad sample truncated the cell for the rest of the run. Cumulative is robust to a
                    # single bad game and self-corrects, while still capping a cell that is genuinely
                    # too slow to be production-usable at this depth.
                    # DEPTH FLOOR (--never-condemn-at-or-below). Shallow cells are the ones the trust-depth
                    # decision is derived from, so condemning one is not a saved cost -- it is a hole in
                    # the answer.
                    #
                    # And the cumulative average, which was itself the fix for a STICKY per-batch flag
                    # (see below), still cannot survive a heavy tail. Measured 2026-08-05, TH H5:
                    #   seed 8008   400g   14.8 s/game   first batch     4.4 s
                    #   seed 9009    50g  358.2 s/game   first batch 17875   s  (4.96 HOURS, 25 games)
                    #   seed 10010  400g   15.3 s/game   first batch     1.9 s
                    #   seed 11011   50g  213.4 s/game   first batch 10668   s  (2.96 hours)
                    # ONE pathological game made its batch cost five hours; 17912/50 = 358 > 60 and the
                    # cell was condemned. Every OTHER game in those cells runs at ~0.1 s/game -- the fill
                    # run took them 50 -> 325 in minutes. So condemning threw away ~350 cheap games to
                    # avoid re-running one expensive game that had ALREADY BEEN PAID FOR.
                    #
                    # (An earlier draft of this comment blamed oversubscription -- 20 single-threaded
                    # workers on a 12-core box. That is real but nowhere near sufficient: contention does
                    # not turn a 2.5 s batch into a 5 h one. The cause is the tail, not the load.)
                    #
                    # So: never condemn at or below this depth; let deep cells absorb the trimming.
                    was=c["intractable"]
                    c["intractable"] = (c["depth"] > args.never_condemn_at_or_below
                                        and (c["ms"] / max(c["games"],1)) > args.intractable_sec_per_game)
                    if c["intractable"] and not was: drop_condemned(c)
                done_batches+=1
                write_state()
                if done_batches % 10 == 0:
                    tot=sum(c["games"] for c in cells); ic=sum(1 for c in cells if c["intractable"])
                    print("... %d batches, %d games total, %d cells intractable" % (done_batches,tot,ic),flush=True)
        while len(futs)<args.workers and submit_next(): pass
    ex.shutdown(); write_state()
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
    ap.add_argument("--workers",type=int,default=(os.cpu_count() or 8),
                    help="concurrent single-threaded batch processes (work-stealing pool). Each is a separate "
                         "process loading the keep model (~1GB for antilife); size to RAM. Default = CPU count.")
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
    hgames = args.hgames if args.hgames is not None else args.games
    vgames = args.vgames if args.vgames is not None else args.games
    hgd = {}
    for spec in args.hgames_depth:
        k, v = spec.split(":"); hgd[int(k)] = int(v)
    os.makedirs(os.path.dirname(args.out),exist_ok=True)
    of=open(args.out,"a")
    run_lines=[]   # THIS run's blocks only (for --write-profile; the --out file is append-history)
    def emit(s): print(s,flush=True); of.write(s+"\n"); of.flush(); run_lines.append(s)
    # Header's `games=` carries VGAMES (the full value arm -> the writer's provenance); heuristic games trail it.
    hnote = ("hgames=%d" % hgames) + ("" if not hgd else " hgames_depth=" + ",".join("%d:%d" % (d, hgd[d]) for d in sorted(hgd)))
    emit("\n===== DEPTH MATRIX (UNBOUNDED)  games=%d seeds=%s value_min_depth=%d%s  %s =====" % (
        vgames, args.seeds, args.value_min_depth,
        "  [PURE value-leaf, no redo]" if args.value_min_depth == 0 else "  [HYBRID redo below this]", hnote))
    for dname in args.decks:
        deck,prof,mt=DECKS[dname]
        # accumulate mean LP + ms per config over seeds
        H={d:[0.0,0.0] for d in args.hdepths}; V={d:[0.0,0.0] for d in args.vdepths}; n=0
        for seed in args.seeds:
            try:
                for d in args.hdepths:
                    g = hgd.get(d, hgames)   # per-depth games: cut only the slow heuristic cells
                    lp,ms=run(deck,d,g,seed,mt,args.threads,None,False,args.value_min_depth); H[d][0]+=lp; H[d][1]+=ms
                for d in args.vdepths:
                    lp,ms=run(deck,d,vgames,seed,mt,args.threads,prof,True,args.value_min_depth); V[d][0]+=lp; V[d][1]+=ms
                n+=1
            except Exception as e:
                emit("  %s s%d ERROR %s" % (dname,seed,e))
        if not n: continue
        emit("---- %s (mean over %d seeds) ----" % (dname,n))
        emit("  heuristic:  " + "   ".join("H%d=%.4f[%.1fms]"%(d,H[d][0]/n,H[d][1]/n) for d in args.hdepths))
        emit("  value-leaf: " + "   ".join("V%d=%.4f[%.1fms]"%(d,V[d][0]/n,V[d][1]/n) for d in args.vdepths))
        emit("  Vi-Hj matrix (neg = value-leaf better):")
        emit("        " + "  ".join("H%d    "%d for d in args.hdepths))
        for vi in args.vdepths:
            row="   V%d  " % vi
            for hj in args.hdepths:
                row += "%+.4f " % (V[vi][0]/n - H[hj][0]/n)
            emit(row)
    of.close()

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
