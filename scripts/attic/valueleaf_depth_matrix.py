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
# Per-deck folder layout (decks/<name>/<name>...): the analyzer writes the profile/value next to the deck
# and the engine resolves siblings directory-relative. (Earlier flat paths decks/<name>.value.json are stale.)
DECKS = {
    "antilife": ("decks/Anti-Lifegain/Anti-Lifegain.cod", "decks/Anti-Lifegain/Anti-Lifegain.value.json", 8),
    "slivers":  ("decks/slivers_vial/slivers_vial.txt",   "decks/slivers_vial/slivers_vial.value.json",   8),
    "TH":       ("decks/treasure_hunt/treasure_hunt.txt", "decks/treasure_hunt/treasure_hunt.value.json", 8),
    "burn":     ("decks/burn/burn.txt",                   "decks/burn/burn.value.json",                   8),
    "knights":  ("decks/Knights/Knights.cod",             "decks/Knights/Knights.value.json",             8),
    # hinata (combo): heuristic is EXPENSIVE at deep depths (H5 ~4.8 s/game vs H2 ~0.1 s) while the value
    # leaf is cheap at every depth. Use separate --hdepths (cap at 3) / --vdepths (1..5) to keep it tractable;
    # the fallback decision only needs V5-vs-H2 and H_conv (heuristic converges by d3). See value.json note.
    "hinata":   ("decks/Hinata2/Hinata2.cod",             "decks/Hinata2/Hinata2.value.json",             8),
    # dragonstorm (storm/combo): like hinata/antilife the deep heuristic is expensive; use capped --hdepths
    # and --vdepths 1..5 for the fallback calibration (V5-vs-H_conv). value model generated 2026-07-22.
    "dragonstorm": ("decks/Dragonstorm/Dragonstorm.cod",  "decks/Dragonstorm/Dragonstorm.value.json",     8),
    # auras (Bogles hexproof-aura aggro): board-driven like burn/slivers -> the value leaf is inert on
    # quality (win decided by visible board+hand, not library uncertainty), a pure search SPEED win.
    "auras":    ("decks/Auras/Auras.cod",                 "decks/Auras/Auras.value.json",                 8),
    # goblins (aggro): board-driven like burn/slivers/auras -> value leaf expected inert on quality,
    # a search SPEED win. hdepths capped at 3 (heuristic converges by d3 for aggro). value model 2026-07-31.
    "goblins":  ("decks/Goblins/Goblins.cod",             "decks/Goblins/Goblins.value.json",             8),
    # creature_giving (gift-the-opponent drain engine): first model 2026-08-06 (no live sidecar yet --
    # use the _staged key until adoption). Deck folder name contains a space.
    "creature_giving": ("decks/Creature Giving/Creature Giving.cod",
                        "decks/Creature Giving/Creature Giving.value.json",                             8),
}

# AUTO-REGISTER every deck folder that is not named above, keyed by its lower-cased folder name with
# non-alphanumerics collapsed to "_" (so "decks/FiveColour" -> "fivecolour", "decks/Creature Giving"
# -> "creature_giving"). Adding a deck to this table used to be a hand edit, and forgetting it was a
# silent trap: the H-cell ladder is guarded on `os.path.exists(<value.json>)`, so an unregistered or
# mis-pathed deck does not error -- it quietly runs every H cell on the slow path. Explicit entries
# above still win, because several carry a non-default max_turns or a comment worth keeping.
PROFILES_AUTO = {}


def _slug(name):
    return "".join(c.lower() if c.isalnum() else "_" for c in name)

_explicit_decks = {v[0] for v in DECKS.values()}
for _dir in sorted(glob.glob("decks/*")):
    if not os.path.isdir(_dir):
        continue
    _stem = os.path.basename(_dir)
    _deck = next((p for p in ("%s/%s.cod" % (_dir, _stem), "%s/%s.txt" % (_dir, _stem))
                  if os.path.exists(p)), None)
    if not _deck or _deck in _explicit_decks:
        continue
    if not os.path.exists("%s/%s.profile.json" % (_dir, _stem)):
        continue          # no profile -> cannot be measured at shipped play; skip rather than guess
    _key = _slug(_stem)
    if _key in DECKS:
        continue
    DECKS[_key] = (_deck, "%s/%s.value.json" % (_dir, _stem), 8)
    PROFILES_AUTO[_key] = "%s/%s.profile.json" % (_dir, _stem)

# STAGED variants: "<deck>_staged" is the same deck + the same mulligan profile, but its V cells read
# a value model that is NOT yet installed in the deck folder (logs/eval/<stem>.value.STAGED.json). The
# model reaches the engine via MTG_VALUE_PROFILE, so pointing a deck key at the staged path is all it
# takes to measure a table for a model BEFORE adopting it -- the order the regeneration queue requires
# (measure, then adopt). H cells set MTG_VALUE_MODEL=0, so they are byte-identical to the plain key;
# only V cells move. Derived for every deck rather than listed, so a new deck gets one for free.
for _k, (_deck, _val, _mt) in list(DECKS.items()):
    _stem = os.path.basename(_val).replace(".value.json", "")
    DECKS[_k + "_staged"] = (_deck, "logs/eval/%s.value.STAGED.json" % _stem, _mt)


# DECK PROFILE (<name>.profile.json). ADDED 2026-07-31: neither run() nor run_batch() used to pass
# --profile at all, so EVERY value-leaf depth table in this repo was measured on a deck with NO
# profile -- no mulligan/keep model, no card_scores. That is not the deck we ship, and it is exactly
# why Hinata's table needed regenerating once its exhaustive keep model existed. The engine resolves
# the keepmodel/value/eval siblings directory-relative off this path (see CLAUDE.md), so passing the
# profile is all that is needed to measure SHIPPED play.
PROFILES = {
    "antilife":    "decks/Anti-Lifegain/Anti-Lifegain.profile.json",
    "slivers":     "decks/slivers_vial/slivers_vial.profile.json",
    "TH":          "decks/treasure_hunt/treasure_hunt.profile.json",
    "burn":        "decks/burn/burn.profile.json",
    "knights":     "decks/Knights/Knights.profile.json",
    "hinata":      "decks/Hinata2/Hinata2.profile.json",
    "dragonstorm": "decks/Dragonstorm/Dragonstorm.profile.json",
    "auras":       "decks/Auras/Auras.profile.json",
    "hinata_staged":   "decks/Hinata2/Hinata2.profile.json",
    "antilife_staged": "decks/Anti-Lifegain/Anti-Lifegain.profile.json",
    "creature_giving":        "decks/Creature Giving/Creature Giving.profile.json",
    "creature_giving_staged": "decks/Creature Giving/Creature Giving.profile.json",
}

# Auto-discovered decks (above) supply their own profile, for both the plain and _staged keys.
# Attaching the profile is not optional: measuring profile-less silently describes a deck we do not
# ship, which invalidated every value-leaf table in this repo once already.
for _k, _p in PROFILES_AUTO.items():
    PROFILES.setdefault(_k, _p)
    PROFILES.setdefault(_k + "_staged", _p)

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
                lp=sum(c["lp_sum"]/c["batches"] for c in cs)/len(cs)
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
        c.update(games=0, lp_sum=0.0, batches=0, ms=0.0, intractable=False, running=False, first_wall=None)
    state_path=args.out+".cells.json"
    if os.path.exists(state_path):                       # resume: skip already-committed batches
        try:
            saved={_cell_key(x):x for x in json.load(open(state_path))}
            for c in cells:
                s=saved.get(_cell_key(c))
                if s: c.update(games=s["games"],lp_sum=s["lp_sum"],batches=s["batches"],ms=s["ms"],
                               intractable=s["intractable"],first_wall=s.get("first_wall"))
        except Exception: pass
        print("resumed %d cells from %s" % (sum(1 for c in cells if c["batches"]), state_path))

    lock=threading.Lock()
    for c in cells: c["inflight"]=0; c["submitted"]=c["batches"]
    def target(c): return min(args.reference_target,args.target) if c["intractable"] else args.target
    def wanted(c): return c["games"] + c["inflight"]*args.batch < target(c)
    def cell_cap():
        # How many batches of ONE cell may be in flight. Batches are DISJOINT game ranges (offset =
        # submitted*batch), so concurrency within a cell is safe -- and the old one-batch-per-cell
        # rule starved the pool the moment the matrix narrowed to a few expensive cells: measured 3
        # of 24 cores busy with 10 cells left, because max parallelism WAS the cell count. Spread the
        # workers over whatever still needs games.
        n=sum(1 for c in cells if wanted(c))
        if n<=0: return 1
        return max(1, -(-args.workers // n))
    def needs(c):  return wanted(c) and c["inflight"] < cell_cap()

    def write_state():
        tmp=state_path+".tmp"
        json.dump([{k:c[k] for k in ("deck","arm","depth","seed","games","lp_sum","batches","ms",
                                     "intractable","first_wall")} for c in cells], open(tmp,"w"))
        os.replace(tmp,state_path)
        emit_table(cells,args)

    ex=ThreadPoolExecutor(max_workers=args.workers)
    futs={}
    def submit_next():
        cand=[c for c in cells if needs(c)]
        if not cand: return False
        c=min(cand,key=lambda x:(x["games"]+x["inflight"]*args.batch,x["depth"]))  # breadth-first
        c["inflight"]+=1
        off=c["submitted"]*args.batch      # NOT batches: several may be in flight for this cell
        c["submitted"]+=1
        deck_file,prof,mt=DECKS[c["deck"]]
        futs[ex.submit(run_batch,deck_file,mt,c["depth"],c["seed"],off,args.batch,
                       c["arm"]=="V",args.value_min_depth,prof,
                       PROFILES.get(c["deck"]))]=c
        return True

    while len(futs)<args.workers and submit_next(): pass
    done_batches=0
    while futs:
        done,_=wait(list(futs),return_when=FIRST_COMPLETED)
        for fut in done:
            c=futs.pop(fut)
            try: lp,wall,p=fut.result()
            except Exception: lp,wall,p=float("nan"),0.0,0
            with lock:
                c["inflight"]-=1
                if p>0 and lp==lp:
                    c["games"]+=p; c["lp_sum"]+=lp; c["batches"]+=1; c["ms"]+=wall
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
                    c["intractable"] = (c["depth"] > args.never_condemn_at_or_below
                                        and (c["ms"] / max(c["games"],1)) > args.intractable_sec_per_game)
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
    ap.add_argument("--incremental",action="store_true",
                    help="Round-robin 50-game BATCHES across every cell (breadth-first), written incrementally, "
                         "with per-cell tractability marking + cross-cell work-stealing. The way to build a "
                         "table on decks whose deep unbounded search has a heavy tail (antilife). Resumable.")
    ap.add_argument("--batch",type=int,default=50,help="games per incremental batch (default 50)")
    ap.add_argument("--target",type=int,default=None,help="full-sample target games/cell (default = --games)")
    ap.add_argument("--reference-target",type=int,default=100,
                    help="games/cell to stop at once a cell is marked INTRACTABLE (a slow, not-production-usable "
                         "cell still gets a reference value; default 100)")
    ap.add_argument("--intractable-sec-per-game",type=float,default=2.0,
                    help="a batch slower than this (wall sec / game, single-threaded) marks its cell intractable "
                         "-> capped at --reference-target. Checked on every batch (first batch is the main signal).")
    ap.add_argument("--never-condemn-at-or-below",type=int,default=0,
                    help="cells at or below this DEPTH are never marked intractable, however slow they measure. "
                         "These are the cells the trust-depth decision reads, so condemning one leaves a hole in "
                         "the answer rather than saving cost -- and since the check is on WALL time it can fire "
                         "purely because the box was oversubscribed. Set to 5 to protect the d<=5 ladder.")
    ap.add_argument("--workers",type=int,default=(os.cpu_count() or 8),
                    help="concurrent single-threaded batch processes (work-stealing pool). Each is a separate "
                         "process loading the keep model (~1GB for antilife); size to RAM. Default = CPU count.")
    args=ap.parse_args()
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
